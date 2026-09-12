#include "environment_model.h"
#include "config.h"
#include <math.h>

static float fastBaseline = 0.0f;
static float slowBaseline = 0.0f;
static bool  baselineInitialized = false;

static float zcrBaseline = 0.0f;
static float hfBaseline = 0.0f;

static float confidence = 0.0f;
static bool  wasFrozen = false;

// Rolling window of recent RMS values, used only to estimate short-term
// stability (coefficient of variation) for the confidence model.
static float rmsWindow[CONF_VARIANCE_WINDOW];
static int windowIndex = 0;
static int windowFilled = 0;

static bool environmentChanging = false;
static int  changeConfirmCounter = 0;
static int  stableConfirmCounter = 0;

void environmentModelInit() {
    fastBaseline = 0.0f;
    slowBaseline = 0.0f;
    baselineInitialized = false;
    zcrBaseline = 0.0f;
    hfBaseline = 0.0f;
    confidence = 0.0f;
    wasFrozen = false;
    windowIndex = 0;
    windowFilled = 0;
    environmentChanging = false;
    changeConfirmCounter = 0;
    stableConfirmCounter = 0;
}

// Coefficient of variation (stddev/mean) of recent RMS -- a cheap, explainable
// stand-in for "how settled is the environment right now." Not derived from a
// specific paper; it's a practical heuristic like the confidence model as a whole.
static float computeRecentCV() {
    if (windowFilled < 4) return 1.0f;  // not enough data yet -> assume unstable
    int n = windowFilled < CONF_VARIANCE_WINDOW ? windowFilled : CONF_VARIANCE_WINDOW;
    float mean = 0.0f;
    for (int i = 0; i < n; i++) mean += rmsWindow[i];
    mean /= n;
    if (mean < 1.0f) return 0.0f;
    float variance = 0.0f;
    for (int i = 0; i < n; i++) {
        float d = rmsWindow[i] - mean;
        variance += d * d;
    }
    variance /= n;
    return sqrtf(variance) / mean;
}

EnvironmentState environmentModelUpdate(const AudioFrame &frame, bool freeze) {
    EnvironmentState out;
    out.justChanged = false;
    out.justStabilized = false;

    if (!baselineInitialized) {
        fastBaseline = frame.rms;
        slowBaseline = frame.rms;
        zcrBaseline = frame.zcr;
        hfBaseline = frame.highFreqRatio;
        baselineInitialized = true;
    }

    rmsWindow[windowIndex] = frame.rms;
    windowIndex = (windowIndex + 1) % CONF_VARIANCE_WINDOW;
    if (windowFilled < CONF_VARIANCE_WINDOW) windowFilled++;

    if (!freeze) {
        fastBaseline = BASELINE_FAST_ALPHA * frame.rms + (1.0f - BASELINE_FAST_ALPHA) * fastBaseline;
        zcrBaseline  = BASELINE_FAST_ALPHA * frame.zcr + (1.0f - BASELINE_FAST_ALPHA) * zcrBaseline;
        hfBaseline   = BASELINE_FAST_ALPHA * frame.highFreqRatio + (1.0f - BASELINE_FAST_ALPHA) * hfBaseline;
    }
    // Slow baseline always adapts, very slowly -- it represents the long-term
    // "normal" environment and is what the fast baseline is compared against
    // to detect a genuine environment change (Section 13).
    slowBaseline = BASELINE_SLOW_ALPHA * frame.rms + (1.0f - BASELINE_SLOW_ALPHA) * slowBaseline;

    float safeFast = fastBaseline < MIN_BASELINE ? MIN_BASELINE : fastBaseline;
    float safeSlow = slowBaseline < MIN_BASELINE ? MIN_BASELINE : slowBaseline;

    out.energyDeviation = frame.rms / safeFast;
    out.zcrDeviation = fabsf(frame.zcr - zcrBaseline);
    out.hfDeviation = fabsf(frame.highFreqRatio - hfBaseline);

    // ---- environment change detection (dual-timescale comparison) ----
    float ratio = safeFast / safeSlow;
    bool ratioOutOfBand = (ratio >= ENV_CHANGE_RATIO_HI) || (ratio <= ENV_CHANGE_RATIO_LO);
    bool ratioStable = (ratio <= (1.0f + ENV_STABLE_RATIO_BAND)) && (ratio >= (1.0f - ENV_STABLE_RATIO_BAND));

    if (!environmentChanging) {
        if (ratioOutOfBand) {
            changeConfirmCounter++;
            if (changeConfirmCounter >= ENV_CHANGE_CONFIRM_FRAMES) {
                environmentChanging = true;
                out.justChanged = true;
                stableConfirmCounter = 0;
            }
        } else {
            changeConfirmCounter = 0;
        }
    } else {
        if (ratioStable) {
            stableConfirmCounter++;
            if (stableConfirmCounter >= ENV_CHANGE_CONFIRM_FRAMES) {
                environmentChanging = false;
                out.justStabilized = true;
                changeConfirmCounter = 0;
            }
        } else {
            stableConfirmCounter = 0;
        }
    }
    out.environmentChanging = environmentChanging;

    // ---- baseline confidence ----
    // Drops once, sharply, the instant we enter an active (frozen) state --
    // the model is being actively challenged by something unusual.
    if (freeze && !wasFrozen) {
        confidence *= CONF_DROP_ON_EVENT;
    }
    wasFrozen = freeze;

    float cv = computeRecentCV();
    float stabilityTarget = cv > 1.0f ? 0.0f : (1.0f - cv);

    if (environmentChanging) {
        // Actively decay toward 0 while we know the environment is shifting --
        // the current baseline is known to be a poor model right now.
        confidence += (0.0f - confidence) * (CONF_RISE_RATE * 1.5f);
    } else if (!freeze) {
        confidence += (stabilityTarget - confidence) * CONF_RISE_RATE;
    }
    if (confidence < 0.0f) confidence = 0.0f;
    if (confidence > 1.0f) confidence = 1.0f;
    out.baselineConfidence = confidence;

    out.fastBaseline = fastBaseline;
    out.slowBaseline = slowBaseline;
    return out;
}
