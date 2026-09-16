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

// PHASE 1 (diagnostic): which vector actually drives the rank/margin/
// direction decision. NORMALIZED_ENERGY is the existing, preserved default
// -- selecting it reproduces current behavior exactly, unchanged. The other
// two exist to answer one question: does removing adaptive-baseline
// division (dividing by each mic's own baseline) fix long-range direction,
// or is calibrated RMS itself already wrong at distance?
enum class DirectionMode {
    NORMALIZED_ENERGY,          // Mode A (default/fallback) -- calibratedRMS / own adaptive baseline
    CALIBRATED_RMS,             // Mode B -- calibrated RMS directly, no baseline division, single frame
    SMOOTHED_CALIBRATED_RMS     // Mode C -- calibrated RMS, no baseline division, short temporal window
};

void setDirectionMode(DirectionMode mode);
DirectionMode getDirectionMode();
const char* directionModeName(DirectionMode m);

// PHASE 2: 3-band spatial diagnostics. Indices into bandEnergy/bandDirection/
// bandConfidence below. NOT wired into the production `direction` field --
// see docs/spatial_architecture_research.md Phase 4 for the integration
// gate (only if Phase 3 measurements show real benefit).
#define BAND_LOW    0
#define BAND_MID    1
#define BAND_HIGH   2
#define BAND_COUNT  3
const char* bandName(int band);

struct SpatialResult {
    // All three vectors are always computed, regardless of which mode is
    // active, so diagnostics can compare them side by side.
    float normalizedEnergy[MIC_COUNT];
    float calibratedRms[MIC_COUNT];
    float smoothedCalibratedRms[MIC_COUNT];
    float baseline[MIC_COUNT];   // each mic's own adaptive baseline (Mode A's denominator)

    Direction direction;             // decided using whichever mode is currently active (PRODUCTION)
    float directionConfidence;
    float dominanceMargin;           // the actual margin value behind the direction decision
    SpatialClass spatialClass;
    int leadingMic;   // loudest mic this frame, per the ACTIVE mode's vector
    int runnerUpMic;  // second-loudest, per the ACTIVE mode's vector -- combined for detection (see buildAggregateFrame)

    // ---- PHASE 2: per-band diagnostics, computed alongside but NOT used
    // for the production `direction` field above ----
    float bandEnergy[BAND_COUNT][MIC_COUNT];   // calibrated band energy per mic, no baseline division
    Direction bandDirection[BAND_COUNT];
    // Band confidence uses the EXACT SAME dominance-margin formula as the
    // main directionConfidence, applied to that band's own energy vector.
    // It represents how clearly one mic dominates WITHIN that band on this
    // one frame -- it is NOT a measured historical reliability score for
    // the band (we don't have that data until Phase 3's distance tests).
    float bandConfidence[BAND_COUNT];
    Direction broadbandDirectionForCompare;  // calibrated-RMS direction, no band split -- fair baseline for comparison
    // Naive placeholder fusion: whichever band has the highest confidence
    // this frame wins. This is a comparison baseline for Phase 3, NOT a
    // tuned production fusion algorithm -- there is no data yet to justify
    // real per-band weights (same reasoning the ToA fusion section applied).
    Direction fusedDirection;
    float fusedConfidence;
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
