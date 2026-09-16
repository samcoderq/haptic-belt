#include "runtime_config.h"
#include "config.h"

float g_noticeEnterThreshold = NOTICE_ENTER_THRESHOLD;
float g_noticeExitThreshold  = NOTICE_EXIT_THRESHOLD;
float g_candidateThreshold   = CANDIDATE_THRESHOLD;
float g_instantThreshold     = INSTANT_THRESHOLD;
float g_deactivateThreshold  = DEACTIVATE_THRESHOLD;

// Per-mode multiplier applied on top of config.h's own defaults, before the
// user's sensitivity slider is applied. First-pass starting points, not
// derived from any measurement -- same "not fabricated" honesty this
// project holds everywhere else: OUTDOOR/WORKPLACE are typically noisier
// environments, so they need to be LESS sensitive (higher thresholds) to
// avoid drowning in false positives; SLEEP should be MORE sensitive (lower
// thresholds) since a sleeping deaf user needs even a moderate sound
// surfaced, and ambient noise is usually lowest then.
static float modeMultiplier(OperatingMode mode) {
    switch (mode) {
        case OperatingMode::HOME:      return 1.0f;
        case OperatingMode::OUTDOOR:   return 1.4f;
        case OperatingMode::WORKPLACE: return 1.3f;
        case OperatingMode::SLEEP:     return 0.7f;
    }
    return 1.0f;
}

void applyRuntimeSensitivity(OperatingMode mode, float sensitivity) {
    if (sensitivity < 0.0f) sensitivity = 0.0f;
    if (sensitivity > 1.0f) sensitivity = 1.0f;

    // sensitivity=0.5 (BeltSettings' own default) reproduces config.h's
    // original defaults exactly when mode=HOME: sensitivityScale=1.0 at the
    // midpoint, ranging from 1.5x (sensitivity=0, least sensitive) down to
    // 0.5x (sensitivity=1, most sensitive) around it.
    float sensitivityScale = 1.5f - sensitivity;
    float scale = modeMultiplier(mode) * sensitivityScale;

    g_noticeEnterThreshold = NOTICE_ENTER_THRESHOLD * scale;
    g_noticeExitThreshold  = NOTICE_EXIT_THRESHOLD * scale;
    g_candidateThreshold   = CANDIDATE_THRESHOLD * scale;
    g_instantThreshold     = INSTANT_THRESHOLD * scale;
    g_deactivateThreshold  = DEACTIVATE_THRESHOLD * scale;
}
