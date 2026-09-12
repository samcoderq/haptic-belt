#include "priority_engine.h"
#include "config.h"

Priority evaluatePriority(float eventScore, float eventConfidence, int sameDirectionRepeatCount,
                           int eventPersistFrames, bool isApproaching) {
    float persistComponent = eventPersistFrames / 20.0f;  // ~2s sustained -> maxes out
    persistComponent = persistComponent > 1.0f ? 1.0f : persistComponent;

    // Maxes out at REPEAT_COUNT_THRESHOLD (the same count that triggers the
    // REPEATED ACTIVITY announcement) rather than a higher number -- so the
    // moment repetition is confirmed at all, it's already carrying its full
    // weight, not still ramping up.
    float repeatComponent = sameDirectionRepeatCount / (float)REPEAT_COUNT_THRESHOLD;
    repeatComponent = repeatComponent > 1.0f ? 1.0f : repeatComponent;

    // Approaching is an explicit bump, not just another weighted term -- a
    // sound getting louder from one spot is qualitatively more urgent than
    // the same instantaneous loudness held steady (a dog barking closer each
    // time vs. one barking at a constant distance).
    float approachingBonus = isApproaching ? 0.20f : 0.0f;

    float combined = 0.50f * eventScore + 0.20f * persistComponent + 0.30f * repeatComponent + approachingBonus;
    combined = combined > 1.0f ? 1.0f : combined;

    // Low-confidence detections (sub-signals disagree) get de-weighted rather
    // than discarded -- a weak-agreement HIGH doesn't get promoted, but a
    // strong-agreement one isn't punished either.
    combined *= (0.5f + 0.5f * eventConfidence);

    if (combined >= PRIORITY_CRITICAL_CUT) return Priority::PRIORITY_CRITICAL;
    if (combined >= PRIORITY_HIGH_CUT) return Priority::PRIORITY_HIGH;
    if (combined >= PRIORITY_MEDIUM_CUT) return Priority::PRIORITY_MEDIUM;
    return Priority::PRIORITY_LOW;
}

const char* priorityName(Priority p) {
    switch (p) {
        case Priority::PRIORITY_LOW:      return "LOW";
        case Priority::PRIORITY_MEDIUM:   return "MEDIUM";
        case Priority::PRIORITY_HIGH:     return "HIGH";
        case Priority::PRIORITY_CRITICAL: return "CRITICAL";
    }
    return "?";
}
