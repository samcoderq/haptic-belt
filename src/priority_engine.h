#pragma once

// priority_engine module: combines acoustic significance (event score),
// how much we trust it (event confidence), and temporal pattern
// (persistence, repetition) into a preliminary priority tier.
//
// IMPORTANT: this is ACOUSTIC SIGNIFICANCE, not REAL-WORLD IMPORTANCE. A
// single very loud unknown sound is capped at HIGH by design -- CRITICAL
// requires corroborating persistence/repetition, not loudness alone. Real
// danger assessment needs classification + context, which don't exist yet.
enum class Priority { PRIORITY_LOW, PRIORITY_MEDIUM, PRIORITY_HIGH, PRIORITY_CRITICAL };

Priority evaluatePriority(float eventScore, float eventConfidence, int repeatCount, int eventPersistFrames);
const char* priorityName(Priority p);
