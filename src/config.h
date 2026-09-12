#pragma once

// ================================================================
// Single point of configuration for every tunable in the pipeline.
// Nothing below is a "final" number -- these are starting points to
// retune against real test data (see TEST A-J in the writeup).
// ================================================================

// ---------- Output mode ----------
// 0 = full multi-section dashboard, 1 = compact one-line-per-frame
#define COMPACT_OUTPUT 0

// ---------- Audio acquisition ----------
static const int AUDIO_SAMPLE_RATE   = 16000;
static const int AUDIO_FRAME_SAMPLES = 1600;  // 100ms @ 16kHz -> 10Hz frame rate

// ---------- Baseline / environment model (dual-timescale EMA) ----------
static const float BASELINE_FAST_ALPHA = 0.02f;   // ~5s time constant -- short-term ambient level, used for deviation
static const float BASELINE_SLOW_ALPHA = 0.002f;  // ~50s time constant -- long-term "normal" environment reference
static const float MIN_BASELINE        = 50.0f;   // floor so ratios don't blow up near silence

// Environment-change detection: compares fast vs slow baseline
static const float ENV_CHANGE_RATIO_HI       = 1.5f;   // fast/slow above this -> got louder
static const float ENV_CHANGE_RATIO_LO       = 0.67f;  // fast/slow below this -> got quieter
static const int   ENV_CHANGE_CONFIRM_FRAMES = 15;     // ~1.5s sustained before declaring a real change
static const float ENV_STABLE_RATIO_BAND     = 0.15f;  // within +-15% of slow baseline counts as "stable"

// ---------- Baseline confidence ----------
static const int   CONF_VARIANCE_WINDOW = 30;    // frames (~3s) used for recent-stability estimate
static const float CONF_RISE_RATE       = 0.03f;
static const float CONF_DROP_ON_EVENT   = 0.5f;  // multiplier applied once when an active state begins

// ---------- Event score fusion ----------
static const float DEVIATION_FOR_SCORE_1    = 6.0f;  // RMS/baseline ratio that saturates the energy sub-score
static const float ONSET_FOR_SCORE_1        = 4.0f;  // frame-to-frame deviation jump that saturates the onset sub-score
static const float SPECTRAL_DEV_FOR_SCORE_1 = 0.6f;  // zcr/high-freq deviation from baseline that saturates spectral sub-score
static const float WEIGHT_ENERGY   = 0.60f;
static const float WEIGHT_ONSET    = 0.25f;
static const float WEIGHT_SPECTRAL = 0.15f;

// ---------- Three-tier awareness thresholds ----------
// Recalibrated against a real test: a metal-plate hit from ~1m scored
// 0.85-0.93, the SAME hit from ~3-4m only scored 0.35-0.55 and was being
// completely filtered out. For this to be useful to a deaf user, a
// distinct, moderately loud sound several meters away must register --
// missing it defeats the point far more than an occasional false positive
// from a loud conversation does. Thresholds dropped accordingly; NOTICE's
// sustain-gate (NOTICE_SUSTAIN_FRAMES) is now the main defense against
// nearby-blip false positives, since amplitude alone is a much lower bar.
static const float NOTICE_ENTER_THRESHOLD = 0.12f;
static const float NOTICE_EXIT_THRESHOLD  = 0.06f;
static const float CANDIDATE_THRESHOLD    = 0.25f;
static const float INSTANT_THRESHOLD      = 0.35f;  // catches a single-frame ~3-4m hit directly
static const float DEACTIVATE_THRESHOLD   = 0.15f;
static const int   CONFIRM_FRAMES         = 2;      // consecutive candidate frames needed if not instant
static const int   COOLDOWN_FRAMES        = 8;      // ~800ms after an EVENT ends
static const int   NOTICE_COOLDOWN_FRAMES = 20;     // ~2s minimum gap between NOTICE announcements
static const int   NOTICE_SUSTAIN_FRAMES  = 5;      // ~500ms a weak signal must hold before it's worth surfacing
                                                     // (filters brief nearby blips -- a real deaf user shouldn't
                                                     //  get buzzed for a footstep or a paper shuffle)

// ---------- Repetition detection ----------
static const int REPEAT_WINDOW_FRAMES   = 100;  // ~10s trailing window (in frame-counter units)
static const int REPEAT_COUNT_THRESHOLD = 3;
static const int REPEAT_HISTORY_SIZE    = 8;

// ---------- Mic health ----------
static const int   MIC_ZERO_STREAK_LIMIT   = 10;
static const int   MIC_CLIP_STREAK_LIMIT   = 10;
static const float MIC_CLIP_PEAK_THRESHOLD = 130000.0f;

// ---------- Priority engine ----------
static const float PRIORITY_MEDIUM_CUT   = 0.30f;
static const float PRIORITY_HIGH_CUT     = 0.60f;
static const float PRIORITY_CRITICAL_CUT = 0.85f;
