#pragma once
#include "audio_capture.h"
#include "event_detector.h"

// classifier_interface module: the seam where a real sound classifier plugs
// in later without touching acquisition, environment modeling, event
// detection, temporal reasoning, or priority. No trained classifier exists
// yet, so this always returns UNKNOWN -- per the "don't fake capabilities"
// rule, we are not claiming classification we don't have.
struct ClassificationResult {
    const char* label;
    float confidence;
};

ClassificationResult classifyEvent(const AudioFrame &frame, const EventEvaluation &eval);
