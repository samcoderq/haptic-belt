#pragma once

// Subset of the belt's src/config.h -- only the constants environment_model,
// event_detector and temporal_reasoner actually use, copied verbatim (same
// values) so this standalone single-mic pipeline behaves identically to the
// belt's original single-mic phase.

// ---------- Audio acquisition ----------
static const int AUDIO_SAMPLE_RATE   = 16000;
static const int AUDIO_FRAME_SAMPLES = 1600;  // 100ms @ 16kHz -> 10Hz frame rate

// ---------- Baseline / environment model (dual-timescale EMA) ----------
static const float BASELINE_FAST_ALPHA = 0.02f;
static const float BASELINE_SLOW_ALPHA = 0.002f;
static const float MIN_BASELINE        = 50.0f;

static const float ENV_CHANGE_RATIO_HI       = 1.5f;
static const float ENV_CHANGE_RATIO_LO       = 0.67f;
static const int   ENV_CHANGE_CONFIRM_FRAMES = 15;
static const float ENV_STABLE_RATIO_BAND     = 0.15f;

// ---------- Baseline confidence ----------
static const int   CONF_VARIANCE_WINDOW = 30;
static const float CONF_RISE_RATE       = 0.03f;
static const float CONF_DROP_ON_EVENT   = 0.5f;

// ---------- Event score fusion ----------
static const float DEVIATION_FOR_SCORE_1    = 6.0f;
static const float ONSET_FOR_SCORE_1        = 4.0f;
static const float SPECTRAL_DEV_FOR_SCORE_1 = 0.35f;
static const float WEIGHT_ENERGY   = 0.45f;
static const float WEIGHT_ONSET    = 0.25f;
static const float WEIGHT_SPECTRAL = 0.30f;

// ---------- Three-tier awareness thresholds ----------
static const float NOTICE_ENTER_THRESHOLD = 0.10f;
static const float NOTICE_EXIT_THRESHOLD  = 0.05f;
static const float CANDIDATE_THRESHOLD    = 0.20f;
static const float INSTANT_THRESHOLD      = 0.28f;
static const float DEACTIVATE_THRESHOLD   = 0.12f;
static const int   CONFIRM_FRAMES         = 2;
static const int   COOLDOWN_FRAMES        = 8;
static const int   NOTICE_COOLDOWN_FRAMES = 20;
static const int   NOTICE_SUSTAIN_FRAMES  = 5;

// ---------- Repetition detection ----------
static const int REPEAT_WINDOW_FRAMES   = 100;
static const int REPEAT_COUNT_THRESHOLD = 3;
static const int REPEAT_HISTORY_SIZE    = 8;
