#pragma once
#include "mic_array.h"
#include "audio_capture.h"

// spatial_detector module: turns 4 raw mic readings into normalized
// per-mic energy, a FRONT/RIGHT/BACK/LEFT (or merged-pair, or
// OMNIDIRECTIONAL, or UNKNOWN) direction decision, and a direction
// confidence -- deliberately NOT TDOA/GCC-PHAT (the two I2S buses are not
// sample-synchronized with each other, see docs). This does not replace
// the existing single-channel event_detector; it runs alongside it and
// also produces the "aggregate" AudioFrame (the currently-loudest mic's
// full feature set) that feeds the existing pipeline unchanged.
enum class Direction {
    FRONT, RIGHT, BACK, LEFT,
    FRONT_RIGHT, RIGHT_BACK, BACK_LEFT, LEFT_FRONT,
    OMNIDIRECTIONAL,
    UNKNOWN
};

enum class SpatialClass { LOCAL, GLOBAL };

struct SpatialResult {
    float normalizedEnergy[MIC_COUNT];
    Direction direction;
    float directionConfidence;
    SpatialClass spatialClass;
    int leadingMic;   // loudest mic this frame
    int runnerUpMic;  // second-loudest -- combined with leadingMic for detection (see buildAggregateFrame)
};

void spatialDetectorInit();

// freezeBaselines should mirror temporalReasonerIsActive() (same pattern as
// the single-mic environment_model) -- don't let an active event drag the
// per-mic baselines up while it's happening.
SpatialResult spatialDetectorUpdate(const MicArrayFrame &frame, bool freezeBaselines);

// Builds the AudioFrame fed into the existing environment_model/
// event_detector pipeline. Combines the top 2 mics' amplitude (rms/peak/
// avgAbs), not just the single loudest -- a sound sitting directly between
// two mics splits its energy across both, so neither alone shows as strong
// a deviation as a sound aimed straight at one mic would, even though the
// total energy reaching the belt is the same. zcr/highFreqRatio still come
// from the leading mic only (timbre isn't additive the way energy is).
AudioFrame buildAggregateFrame(const MicArrayFrame &frame, int leadingMic, int runnerUpMic);

const char* directionName(Direction d);
const char* spatialClassName(SpatialClass s);
