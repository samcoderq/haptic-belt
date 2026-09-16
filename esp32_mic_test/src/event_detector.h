#pragma once
#include "environment_model.h"

// event_detector module: fuses environment-relative deviations into a single
// "how acoustically unusual is this frame" score, plus a separate confidence
// in that score based on whether the underlying signals agree with each other.
// This does NOT decide state (BACKGROUND/NOTICE/EVENT) -- that's
// temporal_reasoner's job, working frame-by-frame off eventScore.
struct EventEvaluation {
    float eventScore;      // 0..1, blended -- "how unusual is this frame"
    float eventConfidence; // 0..1 -- how well the sub-signals agree with each other
    float energyScore;     // sub-score: loudness deviation
    float onsetScore;      // sub-score: how suddenly deviation is rising
    float spectralScore;   // sub-score: zcr/high-freq deviation from learned baseline
};

void eventDetectorInit();
EventEvaluation evaluateEvent(const EnvironmentState &env);
