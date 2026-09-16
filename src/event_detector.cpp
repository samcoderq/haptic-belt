#include "event_detector.h"
#include "config.h"
#include <math.h>

static float prevDeviation = 1.0f;

void eventDetectorInit() {
    prevDeviation = 1.0f;
}

static inline float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

EventEvaluation evaluateEvent(const EnvironmentState &env) {
    EventEvaluation out;

    // Energy sub-score: how far above the fast baseline is the current level.
    float energyScore = clamp01((env.energyDeviation - 1.0f) / (DEVIATION_FOR_SCORE_1 - 1.0f));

    // Onset sub-score: is deviation *rising sharply* right now (temporal cue),
    // not just "is it loud." A slow ramp (like a fading-in fan) scores low
    // here even if it eventually pushes energyScore up.
    float onsetRaw = env.energyDeviation - prevDeviation;
    prevDeviation = env.energyDeviation;
    float onsetScore = clamp01(onsetRaw / ONSET_FOR_SCORE_1);

    // Spectral sub-score: cheap stand-in for "does this sound have a
    // different timbre than the learned background" using ZCR + high-freq
    // energy ratio deviation (no FFT -- see audio_capture.cpp for why).
    float spectralRaw = (env.zcrDeviation + env.hfDeviation) / 2.0f;
    float spectralScore = clamp01(spectralRaw / SPECTRAL_DEV_FOR_SCORE_1);

    float combined = clamp01(
        WEIGHT_ENERGY * energyScore +
        WEIGHT_ONSET * onsetScore +
        WEIGHT_SPECTRAL * spectralScore
    );

    // Damp by how much we trust the current baseline right now (see
    // CONF_SCORE_DAMPING_MIN in config.h). A noisy/just-changed environment
    // (low env.baselineConfidence) needs stronger evidence before this counts
    // as a real event, specifically so the baseline gets a chance to adapt to
    // the new noise floor instead of being repeatedly frozen by false triggers.
    float confidenceScale = CONF_SCORE_DAMPING_MIN + (1.0f - CONF_SCORE_DAMPING_MIN) * env.baselineConfidence;
    combined = clamp01(combined * confidenceScale);

    // Confidence: how tightly the three sub-scores agree. All three high (a
    // genuine broadband sudden loud sound) -> high confidence. Only one
    // spikes while the others stay near zero (e.g. a narrowband glitch) ->
    // low confidence, even if the combined score alone looks event-like.
    float maxS = fmaxf(energyScore, fmaxf(onsetScore, spectralScore));
    float minS = fminf(energyScore, fminf(onsetScore, spectralScore));
    float confidence = clamp01(1.0f - (maxS - minS));

    out.eventScore = combined;
    out.eventConfidence = confidence;
    out.energyScore = energyScore;
    out.onsetScore = onsetScore;
    out.spectralScore = spectralScore;
    return out;
}
