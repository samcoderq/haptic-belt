#include "audio_capture.h"
#include "config.h"
#include <driver/i2s.h>
#include <math.h>
#include <stdlib.h>

// MIC_FRONT on I2S bus 0 -- unchanged from the verified-working wiring.
static const int I2S_SCK_PIN = 4;
static const int I2S_WS_PIN  = 5;
static const int I2S_SD_PIN  = 6;
static const i2s_port_t I2S_PORT = I2S_NUM_0;

static int32_t rawBuf[AUDIO_FRAME_SAMPLES];

// Persistent one-pole high-pass filter state, carried across frames since it's
// a running filter over a continuous stream, not something reset per-buffer.
// Fixed alpha chosen empirically for "cheap, rough high-vs-low energy split" --
// not derived from an exact cutoff-frequency design, intentionally so: a real
// spectral centroid/flux would need an FFT, which is out of budget right now.
static float hpPrevX = 0.0f;
static float hpPrevY = 0.0f;
static const float HP_ALPHA = 0.9f;

// Mic health streak counters (Section 14: sanity checks, not overengineered).
static int zeroStreak = 0;
static int clipStreak = 0;

bool audioInit() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = (uint32_t)AUDIO_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK_PIN,
        .ws_io_num = I2S_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD_PIN
    };

    if (i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL) != ESP_OK) return false;
    if (i2s_set_pin(I2S_PORT, &pin_config) != ESP_OK) return false;
    i2s_zero_dma_buffer(I2S_PORT);
    return true;
}

bool audioReadFrame(AudioFrame &frame) {
    size_t bytesRead = 0;
    esp_err_t err = i2s_read(I2S_PORT, rawBuf, sizeof(rawBuf), &bytesRead, portMAX_DELAY);
    if (err != ESP_OK || bytesRead == 0) return false;

    int samplesRead = bytesRead / sizeof(int32_t);
    double sumSq = 0;
    double sumAbs = 0;
    double hpSumSq = 0;
    int32_t peak = 0;
    int zeroCrossings = 0;
    int32_t prevSample = 0;
    bool havePrev = false;

    for (int i = 0; i < samplesRead; i++) {
        int32_t s = rawBuf[i] >> 14;  // INMP441: 24-bit sample in the top bits of the 32-bit frame

        sumSq += (double)s * (double)s;
        int32_t a = abs(s);
        sumAbs += a;
        if (a > peak) peak = a;

        if (havePrev && ((s >= 0) != (prevSample >= 0))) {
            zeroCrossings++;
        }
        prevSample = s;
        havePrev = true;

        float x = (float)s;
        float y = HP_ALPHA * (hpPrevY + x - hpPrevX);
        hpPrevX = x;
        hpPrevY = y;
        hpSumSq += (double)y * (double)y;
    }

    frame.rms = (float)sqrt(sumSq / samplesRead);
    frame.peak = (float)peak;
    frame.avgAbs = (float)(sumAbs / samplesRead);
    frame.zcr = samplesRead > 1 ? (float)zeroCrossings / (float)(samplesRead - 1) : 0.0f;

    double rawEnergy = sumSq + 1e-6;
    float ratio = (float)(hpSumSq / rawEnergy);
    frame.highFreqRatio = ratio > 1.0f ? 1.0f : (ratio < 0.0f ? 0.0f : ratio);

    // Mic health: sustained zero output or sustained clipping both indicate a
    // wiring/hardware problem rather than real audio content.
    if (frame.rms <= 0.5f) { zeroStreak++; } else { zeroStreak = 0; }
    if (frame.peak >= MIC_CLIP_PEAK_THRESHOLD) { clipStreak++; } else { clipStreak = 0; }
    frame.micOk = (zeroStreak < MIC_ZERO_STREAK_LIMIT) && (clipStreak < MIC_CLIP_STREAK_LIMIT);

    return true;
}
