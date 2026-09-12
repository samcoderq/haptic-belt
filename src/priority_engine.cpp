#include "priority_engine.h"
#include "config.h"

Priority evaluatePriority(float eventScore, float eventConfidence, int repeatCount, int eventPersistFrames) {
    float persistComponent = eventPersistFrames / 20.0f;  // ~2s sustained -> maxes out
    persistComponent = persistComponent > 1.0f ? 1.0f : persistComponent;

    float repeatComponent = repeatCount / 5.0f;  // 5+ onsets in the repeat window -> maxes out
    repeatComponent = repeatComponent > 1.0f ? 1.0f : repeatComponent;

    float combined = 0.6f * eventScore + 0.25f * persistComponent + 0.15f * repeatComponent;
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
