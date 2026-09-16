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

// baselineConfidence was computed here from day one but never actually
// consumed anywhere -- in a noisy/unstable environment (busy room, traffic,
// a fan cycling on) this let a vicious cycle run unchecked: noise causes a
// false CANDIDATE/EVENT -> that freezes the fast baseline (spatial_detector.h's
// freezeBaselines) so it can't adapt to the new noise floor -> deviation from
// the stale baseline stays high -> another false trigger. Damping eventScore
// itself by confidence (event_detector.cpp) breaks the cycle: while confidence
// is low the bar to trigger is effectively raised, so the reasoner spends more
// time in BACKGROUND actually letting the baseline catch up, instead of
// re-freezing it every few seconds. At confidence 1.0 (calm, settled
// environment) this multiplies eventScore by 1.0 -- no change to already-tuned
// quiet-environment behavior.
// Deliberately a mild floor, not an aggressive one: the thresholds above
// were explicitly recalibrated after a real test showed a distant (3-4m)
// genuine event scoring as low as 0.35, and "missing it defeats the point
// far more than an occasional false positive does" (see that note above).
// At this floor, that 0.35 case still clears CANDIDATE_THRESHOLD (0.20) even
// at worst-case zero confidence (0.35 * 0.75 = 0.2625); only scores already
// marginal against the low tier thresholds (NOTICE/CANDIDATE, both <= 0.20)
// get pulled back under during a genuinely unstable/noisy stretch.
static const float CONF_SCORE_DAMPING_MIN = 0.75f;  // eventScore multiplier at baselineConfidence == 0

// ---------- Event score fusion ----------
// Rebalanced: a loud HIGH-PITCHED sound often doesn't produce dramatically
// higher RMS than background noise (a pure tone's peak-to-RMS ratio is much
// lower than a broadband impulsive sound's), but it IS very different in
// character (zero-crossing rate, high-frequency energy) from typical
// ambient noise. Leaning more on that novelty signal, less on loudness
// alone, catches this case without needing loudness to carry all the proof.
static const float DEVIATION_FOR_SCORE_1    = 6.0f;   // RMS/baseline ratio that saturates the energy sub-score
static const float ONSET_FOR_SCORE_1        = 4.0f;   // frame-to-frame deviation jump that saturates the onset sub-score
static const float SPECTRAL_DEV_FOR_SCORE_1 = 0.35f;  // was 0.6 -- smaller zcr/hf deviation now saturates the spectral sub-score
static const float WEIGHT_ENERGY   = 0.45f;  // was 0.60
static const float WEIGHT_ONSET    = 0.25f;
static const float WEIGHT_SPECTRAL = 0.30f;  // was 0.15 -- doubled, novelty alone can now carry a detection

// ---------- Three-tier awareness thresholds ----------
// Recalibrated against a real test: a metal-plate hit from ~1m scored
// 0.85-0.93, the SAME hit from ~3-4m only scored 0.35-0.55 and was being
// completely filtered out. For this to be useful to a deaf user, a
// distinct, moderately loud sound several meters away must register --
// missing it defeats the point far more than an occasional false positive
// from a loud conversation does. Thresholds dropped accordingly; NOTICE's
// sustain-gate (NOTICE_SUSTAIN_FRAMES) is now the main defense against
// nearby-blip false positives, since amplitude alone is a much lower bar.
static const float NOTICE_ENTER_THRESHOLD = 0.10f;
static const float NOTICE_EXIT_THRESHOLD  = 0.05f;
static const float CANDIDATE_THRESHOLD    = 0.20f;
static const float INSTANT_THRESHOLD      = 0.28f;  // lowered further for high-pitched/quieter-by-RMS sounds
static const float DEACTIVATE_THRESHOLD   = 0.12f;
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

// ---------- Direction memory (same-direction repetition + approach trend) ----------
// A real ongoing situation (repeated knocking, a dog barking repeatedly) comes
// from roughly the SAME direction each time -- unrelated one-off sounds from
// different directions should not count toward this. Approaching = the
// matching-direction onsets are getting louder over time (footsteps, a
// vehicle, a barking dog getting closer), which is more urgent than the same
// loudness held steady, independent of absolute volume.
static const int   DIRECTION_HISTORY_SIZE       = 8;
static const float APPROACHING_TREND_THRESHOLD  = 0.30f;  // relative rise between earlier/later onset scores to call it "approaching"

// ---------- Habituation ----------
// Suppresses repeat NOTICE-level announcements (LED flash, serial log, BLE
// eventSeq increment) for a persistent, non-escalating same-direction
// recurrence -- e.g. a fan, a distant repetitive machine sound a hearing
// person would tune out after a while too. Deliberately scoped to
// NOTICE-tier only: CANDIDATE/EVENT-tier onsets always fully announce
// regardless of history (a real escalation is always worth surfacing), and
// also always reset the habituation streak. First-pass values, not tuned
// against real data -- flagged as a gap in docs/research_and_recommendations.md,
// this is the first implementation of it.
static const int HABITUATION_STREAK_THRESHOLD = 6;   // consecutive non-escalating same-direction NOTICE onsets before suppressing
static const int HABITUATION_RESET_FRAMES     = 600; // ~60s real-time gap since the last NOTICE onset from that direction resets the streak

// ---------- Mic health ----------
static const int   MIC_ZERO_STREAK_LIMIT   = 10;
static const int   MIC_CLIP_STREAK_LIMIT   = 10;
static const float MIC_CLIP_PEAK_THRESHOLD = 130000.0f;

// ---------- Awareness output (LEDs now, motors later -- see awareness_output.h) ----------
// Same GPIOs originally reserved for motors in the very first architecture
// pass, before any code existed -- avoids mic pins (4,5,6,15,16,17),
// reserved I2C pins (8,9), and all strapping/USB/flash pins.
static const int LED_FRONT_PIN = 7;
static const int LED_RIGHT_PIN = 10;
static const int LED_BACK_PIN  = 13;  // moved from GPIO11 -- suspected bad pin/wiring
static const int LED_LEFT_PIN  = 12;

static const int LED_PWM_VERY_LOW = 15;   // direction confidence < 0.30
static const int LED_PWM_DIM      = 60;   // 0.30-0.50
static const int LED_PWM_MEDIUM   = 150;  // 0.50-0.75
static const int LED_PWM_BRIGHT   = 255;  // > 0.75

static const unsigned long LED_PULSE_HALF_PERIOD_MS = 150;  // on/off half-cycle for pulse patterns
static const unsigned long LED_UNKNOWN_FLASH_MS     = 150;  // brief all-LED flash when direction is UNKNOWN
static const unsigned long LED_KEYWORD_FLASH_MS     = 400;  // all-LED flash on a keyword_detector.cpp match --
                                                              // longer than the UNKNOWN flash so they're
                                                              // perceptibly different, not tuned/validated

// How long main.cpp holds a matched keyword's slot in the BLE State packet's
// reserved byte, instead of just the one frame the match actually happened
// on. Added 2026-09-16 after a real hackathon test: firmware reliably fired
// and logged real matches (Serial showed ">>> KEYWORD DETECTED" repeatedly),
// but the phone's Dashboard kept showing "No keyword detected yet" -- BLE
// State notifies at ~10Hz and a single-frame pulse is exactly the kind of
// update Android's BLE stack can silently drop under sustained notify
// traffic, with nothing to resend it. Holding the value across ~1s / ~10
// notifications instead of 1 makes losing ALL of them far less likely,
// without changing the wire format (still the same single byte, same
// meaning) or any matching/threshold logic.
static const unsigned long KEYWORD_MATCH_BLE_LATCH_MS = 1000;

// ---------- Priority engine ----------
static const float PRIORITY_MEDIUM_CUT   = 0.30f;
static const float PRIORITY_HIGH_CUT     = 0.60f;
static const float PRIORITY_CRITICAL_CUT = 0.85f;

// ---------- Spatial / 4-mic direction ----------
// GLOBAL_MEAN/CV_THRESHOLD were never recalibrated after the event-pipeline
// sensitivity pass, so a moderately loud sound was too easily classified
// OMNIDIRECTIONAL just for raising all 4 mics' normalized energy somewhat
// together. Tightened so OMNIDIRECTIONAL requires a clearly bigger, clearly
// more uniform rise, and DOMINANCE_MARGIN lowered so a real single-direction
// sound commits to a direction more readily.
static const float SPATIAL_BASELINE_ALPHA     = 0.02f;  // per-mic fast EMA, same time constant as single-mic baseline
static const float SPATIAL_MIN_BASELINE       = 50.0f;
static const float SPATIAL_SMOOTHING_ALPHA    = 0.4f;   // was 0.3 -- reacts faster to brief transients (claps), less lag
static const float DOMINANCE_MARGIN           = 0.08f;  // was 0.10 -- commit to a single direction more readily
static const float DOMINANCE_MARGIN_FULL_CONF = 0.45f;  // was 0.5 -- reach full confidence at a smaller margin too
static const float GLOBAL_MEAN_THRESHOLD      = 2.0f;   // was 1.5 -- requires a clearly bigger overall rise
static const float GLOBAL_CV_THRESHOLD        = 0.15f;  // was 0.25 -- requires energies to be much more uniform

// ---------- PHASE 1 diagnostics: direction-mode comparison ----------
static const int SPATIAL_TEMPORAL_WINDOW_FRAMES = 5;   // ~500ms window for Mode C (smoothed calibrated RMS)
static const int SPATIAL_DIAG_PRINT_EVERY_N     = 5;   // throttle: full diagnostic block every Nth frame (~2Hz)

// ---------- PHASE 2: lightweight 3-band spatial features ----------
// Two one-pole low-pass filters, not a true brick-wall filterbank --
// "smallest practical implementation" per instruction, not a 32-band
// design. alpha = 1 - exp(-2*pi*fc/fs) for fs=16000.
//   LOW  band = output of the ~700Hz low-pass filter directly
//   HIGH band = raw signal minus the ~3000Hz low-pass filter's output
//   MID  band = ~3000Hz low-pass output minus ~700Hz low-pass output
// This is an approximation (gentle one-pole rolloff, not a sharp cutoff) --
// documented as such, not claimed to be precise.
static const float BAND_LOW_CUTOFF_ALPHA  = 0.2403f;  // ~700 Hz
static const float BAND_HIGH_CUTOFF_ALPHA = 0.6921f;  // ~3000 Hz
