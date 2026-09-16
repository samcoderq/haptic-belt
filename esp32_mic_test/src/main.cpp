// Single-mic EVENT DETECTION pipeline for the second, plain ESP32-WROOM-32
// (38-pin DevKitC-style) board + INMP441 -- the same adaptive-baseline /
// event-score / temporal-state-machine pipeline built during the belt's
// original single-mic phase (environment_model + event_detector +
// temporal_reasoner, copied verbatim from ../src), driven by this board's
// own proven I2S bring-up. Independent of the belt's 4-mic firmware.
//
// Wiring:
//   INMP441 VDD -> 3V3
//   INMP441 GND -> GND
//   INMP441 L/R -> GND   (selects the left slot in the stereo I2S stream)
//   INMP441 WS  -> GPIO25
//   INMP441 SCK -> GPIO26
//   INMP441 SD  -> GPIO27
//
// Reads the bus in STEREO (I2S_CHANNEL_FMT_RIGHT_LEFT) and uses only the
// left slot -- mono mode reads as silent all-zeros with this driver +
// INMP441 combination (see the bring-up test that preceded this file).
//
// Serial output matches tools/live_plot.py's expected format:
//   python tools/live_plot.py COM16

#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>

#include "config.h"
#include "audio_capture.h"
#include "environment_model.h"
#include "event_detector.h"
#include "temporal_reasoner.h"

#define I2S_WS_PIN   25
#define I2S_SCK_PIN  26
#define I2S_SD_PIN   27
#define I2S_PORT     I2S_NUM_0

// One I2S read per frame -- AUDIO_FRAME_SAMPLES (1600) samples @ 16kHz = 100ms/frame (10Hz).
static int32_t stereoBuf[AUDIO_FRAME_SAMPLES * 2];

static float hpPrevX = 0.0f;
static float hpPrevY = 0.0f;

static void i2sInit() {
  i2s_config_t i2sConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = (uint32_t)AUDIO_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pinConfig = {
    .bck_io_num = I2S_SCK_PIN,
    .ws_io_num = I2S_WS_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD_PIN
  };

  i2s_driver_install(I2S_PORT, &i2sConfig, 0, NULL);
  i2s_set_pin(I2S_PORT, &pinConfig);
  i2s_zero_dma_buffer(I2S_PORT);
}

// Same feature computation as the belt's mic_array.cpp computeChannelStats
// (rms/zcr/highFreqRatio only -- this pipeline doesn't need peak/bands).
static void computeAudioFrame(int samplesRead, AudioFrame &frame) {
  double sumSq = 0, hpSumSq = 0;
  int zeroCrossings = 0;
  int32_t prevSample = 0;
  bool havePrev = false;

  for (int i = 0; i < samplesRead; i++) {
    int32_t raw = stereoBuf[i * 2 + 0]; // left slot (L/R -> GND)
    int32_t s = raw >> 14;

    sumSq += (double)s * (double)s;

    if (havePrev && ((s >= 0) != (prevSample >= 0))) zeroCrossings++;
    prevSample = s;
    havePrev = true;

    float x = (float)s;
    float y = 0.9f * (hpPrevY + x - hpPrevX);
    hpPrevX = x;
    hpPrevY = y;
    hpSumSq += (double)y * (double)y;
  }

  frame.rms = (float)sqrt(sumSq / samplesRead);
  frame.zcr = samplesRead > 1 ? (float)zeroCrossings / (float)(samplesRead - 1) : 0.0f;

  double rawEnergy = sumSq + 1e-6;
  float ratio = (float)(hpSumSq / rawEnergy);
  frame.highFreqRatio = ratio > 1.0f ? 1.0f : (ratio < 0.0f ? 0.0f : ratio);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== ESP32 (38-pin DevKitC) + INMP441 single-mic EVENT DETECTION ===");
  Serial.println("Wiring: VDD->3V3  GND->GND  L/R->GND  WS->GPIO25  SCK->GPIO26  SD->GPIO27");
  i2sInit();
  environmentModelInit();
  eventDetectorInit();
  temporalReasonerInit();
  Serial.println("Pipeline initialized. Run: python tools/live_plot.py COM16");
  Serial.println();
}

void loop() {
  size_t bytesRead = 0;
  i2s_read(I2S_PORT, (void*)stereoBuf, sizeof(stereoBuf), &bytesRead, portMAX_DELAY);
  int totalWords = bytesRead / sizeof(int32_t);
  int samplesRead = totalWords / 2;

  if (samplesRead <= 0) {
    Serial.println("No samples read -- check wiring");
    delay(200);
    return;
  }

  AudioFrame frame;
  computeAudioFrame(samplesRead, frame);

  // Query BEFORE this frame's temporalReasonerUpdate() -- reflects state as
  // of the end of the previous frame, per temporal_reasoner.h's contract.
  bool freeze = temporalReasonerIsActive();
  EnvironmentState env = environmentModelUpdate(frame, freeze);
  EventEvaluation eval = evaluateEvent(env);
  TemporalResult temporal = temporalReasonerUpdate(eval.eventScore);

  Serial.print("RMS: ");
  Serial.println(frame.rms, 2);
  Serial.print("Baseline: ");
  Serial.println(env.fastBaseline, 2);
  Serial.print("Event Score: ");
  Serial.println(eval.eventScore, 3);
  Serial.print("Confidence: ");
  Serial.println(eval.eventConfidence, 3);

  if (temporal.eventAnnounce) {
    Serial.println(">>> EVENT DETECTED <<<");
  }
  if (temporal.repeatAnnounce) {
    Serial.print(">>> REPEATED ACTIVITY <<< count=");
    Serial.print(temporal.repeatCount);
    Serial.print(" pattern=");
    Serial.println(temporal.pattern);
  }

  Serial.print("State: ");
  Serial.println(awarenessStateName(temporal.state));
  Serial.println();
}
