#pragma once
#include "audio_capture.h"

// environment_model module: dual-timescale baseline tracking, baseline
// confidence, and slow-vs-fast environment-change detection. This is the
// "what is normal right now" layer -- it does not decide if something is
// an event, it just characterizes the background against which everything
// else is measured.
struct EnvironmentState {
    float fastBaseline;      // ~5s EMA, frozen while temporal_reasoner is active (CANDIDATE/EVENT)
    float slowBaseline;      // ~50s EMA, always adapting -- the long-term "normal" reference
    float baselineConfidence;
    float energyDeviation;   // rms / fastBaseline
    float zcrDeviation;      // |zcr - learned zcr baseline|
    float hfDeviation;       // |highFreqRatio - learned highFreqRatio baseline|
    bool  environmentChanging;
    bool  justChanged;       // true only on the frame it was first confirmed
    bool  justStabilized;    // true only on the frame it recovered
};

void environmentModelInit();
EnvironmentState environmentModelUpdate(const AudioFrame &frame, bool freeze);
