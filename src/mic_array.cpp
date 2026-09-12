#include "mic_array.h"
#include "config.h"
#include <driver/i2s.h>
#include <math.h>
#include <stdlib.h>

// Bus 0 (existing, unchanged pins): FRONT (left slot, L/R->GND) + RIGHT (right slot, L/R->3V3)
static const int I2S0_SCK_PIN = 4;
static const int I2S0_WS_PIN  = 5;
static const int I2S0_SD_PIN  = 6;
static const i2s_port_t I2S0_PORT = I2S_NUM_0;

// Bus 1 (new): BACK (left slot, L/R->GND) + LEFT (right slot, L/R->3V3)
static const int I2S1_SCK_PIN = 15;
static const int I2S1_WS_PIN  = 16;
static const int I2S1_SD_PIN  = 17;
static const i2s_port_t I2S1_PORT = I2S_NUM_1;

// Interleaved stereo buffers: 2 words (one per channel) per sample position.
static int32_t bus0Buf[AUDIO_FRAME_SAMPLES * 2];
static int32_t bus1Buf[AUDIO_FRAME_SAMPLES * 2];

// Per-mic persistent DSP state (high-pass filter memory + health streaks) --
// four independent copies of exactly what audio_capture.cpp does for one mic.
struct MicDsp {
    float hpPrevX = 0.0f;
    float hpPrevY = 0.0f;
    int   zeroStreak = 0;
    int   clipStreak = 0;
};
static MicDsp dsp[MIC_COUNT];
static float calibrationGain[MIC_COUNT] = {1.0f, 1.0f, 1.0f, 1.0f};

static const char* NAMES[MIC_COUNT] = {"FRONT", "RIGHT", "BACK", "LEFT"};

const char* micName(int idx) {
    if (idx < 0 || idx >= MIC_COUNT) return "?";
    return NAMES[idx];
}

static bool initBus(i2s_port_t port, int sck, int ws, int sd) {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = (uint32_t)AUDIO_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,  // stereo: both mics on this bus
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = sck,
        .ws_io_num = ws,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = sd
    };
    if (i2s_driver_install(port, &i2s_config, 0, NULL) != ESP_OK) return false;
    if (i2s_set_pin(port, &pin_config) != ESP_OK) return false;
    i2s_zero_dma_buffer(port);
    return true;
}

bool micArrayInit() {
    if (!initBus(I2S0_PORT, I2S0_SCK_PIN, I2S0_WS_PIN, I2S0_SD_PIN)) return false;
    if (!initBus(I2S1_PORT, I2S1_SCK_PIN, I2S1_WS_PIN, I2S1_SD_PIN)) return false;
    return true;
}

// Extracts one interleaved channel from a stereo buffer and computes the
// same features audio_capture.cpp computes for the single mic -- one
// implementation, called 4 times with different state, not 4 copies.
//
// NOTE ON channelOffset: unverified against real hardware yet. With
// I2S_CHANNEL_FMT_RIGHT_LEFT some ESP-IDF versions deliver [right, left]
// per pair rather than [left, right]. If FRONT/RIGHT or BACK/LEFT turn out
// swapped in testing, swap the two channelOffset arguments below (0<->1)
// for that bus -- it's a labeling fix, not a wiring problem.
static void computeChannelStats(const int32_t *buf, int totalWords, int channelOffset,
                                 MicrophoneFrame &frame, MicDsp &d) {
    int samplesRead = totalWords / 2;
    double sumSq = 0, sumAbs = 0, hpSumSq = 0;
    int32_t peak = 0;
    int zeroCrossings = 0;
    int32_t prevSample = 0;
    bool havePrev = false;

    for (int i = 0; i < samplesRead; i++) {
        int32_t raw = buf[i * 2 + channelOffset];
        int32_t s = raw >> 14;  // same INMP441 bit alignment as the single-mic path

        sumSq += (double)s * (double)s;
        int32_t a = abs(s);
        sumAbs += a;
        if (a > peak) peak = a;

        if (havePrev && ((s >= 0) != (prevSample >= 0))) zeroCrossings++;
        prevSample = s;
        havePrev = true;

        float x = (float)s;
        float y = 0.9f * (d.hpPrevY + x - d.hpPrevX);
        d.hpPrevX = x;
        d.hpPrevY = y;
        hpSumSq += (double)y * (double)y;
    }

    frame.rms = (float)sqrt(sumSq / samplesRead);
    frame.peak = (float)peak;
    frame.avgAbs = (float)(sumAbs / samplesRead);
    frame.zcr = samplesRead > 1 ? (float)zeroCrossings / (float)(samplesRead - 1) : 0.0f;

    double rawEnergy = sumSq + 1e-6;
    float ratio = (float)(hpSumSq / rawEnergy);
    frame.highFreqRatio = ratio > 1.0f ? 1.0f : (ratio < 0.0f ? 0.0f : ratio);

    if (frame.rms <= 0.5f) d.zeroStreak++; else d.zeroStreak = 0;
    if (frame.peak >= MIC_CLIP_PEAK_THRESHOLD) d.clipStreak++; else d.clipStreak = 0;
    frame.micOk = (d.zeroStreak < MIC_ZERO_STREAK_LIMIT) && (d.clipStreak < MIC_CLIP_STREAK_LIMIT);
}

bool micArrayReadFrame(MicArrayFrame &out) {
    size_t bytes0 = 0, bytes1 = 0;
    esp_err_t err0 = i2s_read(I2S0_PORT, bus0Buf, sizeof(bus0Buf), &bytes0, portMAX_DELAY);
    esp_err_t err1 = i2s_read(I2S1_PORT, bus1Buf, sizeof(bus1Buf), &bytes1, portMAX_DELAY);
    if (err0 != ESP_OK || err1 != ESP_OK || bytes0 == 0 || bytes1 == 0) return false;

    int words0 = bytes0 / sizeof(int32_t);
    int words1 = bytes1 / sizeof(int32_t);

    computeChannelStats(bus0Buf, words0, 0, out.mic[MIC_FRONT], dsp[MIC_FRONT]);
    computeChannelStats(bus0Buf, words0, 1, out.mic[MIC_RIGHT], dsp[MIC_RIGHT]);
    computeChannelStats(bus1Buf, words1, 0, out.mic[MIC_BACK],  dsp[MIC_BACK]);
    computeChannelStats(bus1Buf, words1, 1, out.mic[MIC_LEFT],  dsp[MIC_LEFT]);

    for (int m = 0; m < MIC_COUNT; m++) {
        out.mic[m].rms *= calibrationGain[m];
        out.mic[m].peak *= calibrationGain[m];
        out.mic[m].avgAbs *= calibrationGain[m];
    }

    return true;
}

void micArrayCalibrate(int numFrames, float outGains[MIC_COUNT], float outAvgRms[MIC_COUNT]) {
    double sum[MIC_COUNT] = {0, 0, 0, 0};
    int counted = 0;
    MicArrayFrame f;

    for (int i = 0; i < numFrames; i++) {
        if (micArrayReadFrame(f)) {
            for (int m = 0; m < MIC_COUNT; m++) sum[m] += f.mic[m].rms;
            counted++;
        }
    }

    if (counted == 0) {
        for (int m = 0; m < MIC_COUNT; m++) { outGains[m] = 1.0f; outAvgRms[m] = 0.0f; }
        return;
    }

    float avg[MIC_COUNT];
    float reference = 0.0f;
    for (int m = 0; m < MIC_COUNT; m++) {
        avg[m] = (float)(sum[m] / counted);
        reference += avg[m];
    }
    reference /= MIC_COUNT;

    for (int m = 0; m < MIC_COUNT; m++) {
        float safeAvg = avg[m] < 10.0f ? 10.0f : avg[m];  // avoid a near-silent mic producing a huge gain
        calibrationGain[m] = reference / safeAvg;
        outGains[m] = calibrationGain[m];
        outAvgRms[m] = avg[m];
    }
}
