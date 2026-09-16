#include "spatial_detector.h"
#include "config.h"
#include <math.h>

static float micBaseline[MIC_COUNT];
static float smoothedNorm[MIC_COUNT];
static bool  initialized = false;

// Mode C's short temporal window over calibrated RMS -- a plain moving
// average, not an EMA, per the "robust short temporal window" request
// rather than reusing Mode A's smoothing mechanism.
static float calRmsHistory[MIC_COUNT][SPATIAL_TEMPORAL_WINDOW_FRAMES];
static int   histIndex = 0;
static int   histFilled = 0;

static DirectionMode currentMode = DirectionMode::NORMALIZED_ENERGY;  // preserved default

void setDirectionMode(DirectionMode mode) { currentMode = mode; }
DirectionMode getDirectionMode() { return currentMode; }

const char* directionModeName(DirectionMode m) {
    switch (m) {
        case DirectionMode::NORMALIZED_ENERGY:       return "NORMALIZED_ENERGY";
        case DirectionMode::CALIBRATED_RMS:          return "CALIBRATED_RMS";
        case DirectionMode::SMOOTHED_CALIBRATED_RMS: return "SMOOTHED_CALIBRATED_RMS";
    }
    return "?";
}

void spatialDetectorInit() {
    for (int i = 0; i < MIC_COUNT; i++) {
        micBaseline[i] = 0.0f;
        smoothedNorm[i] = 0.0f;
        for (int w = 0; w < SPATIAL_TEMPORAL_WINDOW_FRAMES; w++) calRmsHistory[i][w] = 0.0f;
    }
    histIndex = 0;
    histFilled = 0;
    initialized = false;
}

// Physically adjacent pairs get a merged direction label. Opposite pairs
// (FRONT/BACK, LEFT/RIGHT) tied doesn't correspond to any real direction the
// belt can represent, so that combination falls through to UNKNOWN.
static Direction mergedDirection(int a, int b) {
    bool has[MIC_COUNT] = {false, false, false, false};
    has[a] = true;
    has[b] = true;
    if (has[MIC_FRONT] && has[MIC_RIGHT]) return Direction::FRONT_RIGHT;
    if (has[MIC_RIGHT] && has[MIC_BACK])  return Direction::RIGHT_BACK;
    if (has[MIC_BACK]  && has[MIC_LEFT])  return Direction::BACK_LEFT;
    if (has[MIC_LEFT]  && has[MIC_FRONT]) return Direction::LEFT_FRONT;
    return Direction::UNKNOWN;
}

static Direction singleDirection(int mic) {
    switch (mic) {
        case MIC_FRONT: return Direction::FRONT;
        case MIC_RIGHT: return Direction::RIGHT;
        case MIC_BACK:  return Direction::BACK;
        case MIC_LEFT:  return Direction::LEFT;
    }
    return Direction::UNKNOWN;
}

const char* directionName(Direction d) {
    switch (d) {
        case Direction::FRONT:           return "FRONT";
        case Direction::RIGHT:           return "RIGHT";
        case Direction::BACK:            return "BACK";
        case Direction::LEFT:            return "LEFT";
        case Direction::FRONT_RIGHT:     return "FRONT-RIGHT";
        case Direction::RIGHT_BACK:      return "RIGHT-BACK";
        case Direction::BACK_LEFT:       return "BACK-LEFT";
        case Direction::LEFT_FRONT:      return "LEFT-FRONT";
        case Direction::OMNIDIRECTIONAL: return "OMNIDIRECTIONAL";
        case Direction::UNKNOWN:         return "UNKNOWN";
    }
    return "?";
}

const char* spatialClassName(SpatialClass s) {
    return s == SpatialClass::LOCAL ? "LOCAL" : "GLOBAL";
}

const char* bandName(int band) {
    switch (band) {
        case BAND_LOW:  return "LOW";
        case BAND_MID:  return "MID";
        case BAND_HIGH: return "HIGH";
    }
    return "?";
}

// Lightweight return value so this logic can be reused for the production
// decision AND each band's diagnostic decision without them overwriting
// each other's fields in SpatialResult.
struct DirectionDecision {
    Direction direction;
    float confidence;
    SpatialClass spatialClass;
    float dominanceMargin;
    int leader;
    int runnerUp;
};

// Shared rank/margin/direction/confidence decision, operating on whichever
// vector is passed in. Identical logic to what existed before this
// refactor when fed `normalizedEnergy` with useAbsoluteMeanCheck=true --
// Mode A's production behavior is bit-for-bit unchanged from pre-Phase-1.
static DirectionDecision decideDirection(const float energy[MIC_COUNT], bool useAbsoluteMeanCheck) {
    DirectionDecision result;

    int order[MIC_COUNT] = {0, 1, 2, 3};
    for (int i = 0; i < MIC_COUNT; i++) {
        for (int j = i + 1; j < MIC_COUNT; j++) {
            if (energy[order[j]] > energy[order[i]]) {
                int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
        }
    }
    int leader = order[0];
    int runnerUp = order[1];
    result.leader = leader;
    result.runnerUp = runnerUp;

    float mean = 0.0f;
    for (int i = 0; i < MIC_COUNT; i++) mean += energy[i];
    mean /= MIC_COUNT;
    float variance = 0.0f;
    for (int i = 0; i < MIC_COUNT; i++) {
        float d = energy[i] - mean;
        variance += d * d;
    }
    variance /= MIC_COUNT;
    float cv = mean > 0.01f ? sqrtf(variance) / mean : 1.0f;

    // GLOBAL_MEAN_THRESHOLD assumes a baseline-normalized vector where
    // "1.0 == normal" -- that reference point only exists for
    // NORMALIZED_ENERGY (and is meaningless for raw/smoothed calibrated RMS
    // or band energy, which have no such fixed point). Those cases fall
    // back to the CV (spread) check alone, which is scale-invariant.
    bool globalDetected = useAbsoluteMeanCheck
                               ? (mean >= GLOBAL_MEAN_THRESHOLD && cv <= GLOBAL_CV_THRESHOLD)
                               : (cv <= GLOBAL_CV_THRESHOLD);

    if (globalDetected) {
        result.direction = Direction::OMNIDIRECTIONAL;
        result.confidence = 1.0f - (cv / GLOBAL_CV_THRESHOLD);
        result.spatialClass = SpatialClass::GLOBAL;
        result.dominanceMargin = 0.0f;
    } else {
        result.spatialClass = SpatialClass::LOCAL;
        float leaderVal = energy[leader];
        float runnerVal = energy[runnerUp];
        float margin = leaderVal > 0.01f ? (leaderVal - runnerVal) / leaderVal : 0.0f;
        result.dominanceMargin = margin;

        if (margin >= DOMINANCE_MARGIN) {
            result.direction = singleDirection(leader);
            float confRange = DOMINANCE_MARGIN_FULL_CONF - DOMINANCE_MARGIN;
            float raw = confRange > 0 ? (margin - DOMINANCE_MARGIN) / confRange : 1.0f;
            result.confidence = raw < 0.0f ? 0.0f : (raw > 1.0f ? 1.0f : raw);
        } else {
            result.direction = mergedDirection(leader, runnerUp);
            result.confidence = 0.3f + 0.4f * (margin / DOMINANCE_MARGIN);
            if (result.direction == Direction::UNKNOWN) {
                result.confidence *= 0.5f;
            }
        }
    }

    return result;
}

SpatialResult spatialDetectorUpdate(const MicArrayFrame &frame, bool freezeBaselines) {
    SpatialResult out;

    if (!initialized) {
        for (int i = 0; i < MIC_COUNT; i++) {
            micBaseline[i] = frame.mic[i].rms;
        }
        initialized = true;
    }

    // ---- Mode A input: normalized energy (unchanged calculation) ----
    for (int i = 0; i < MIC_COUNT; i++) {
        if (!freezeBaselines) {
            micBaseline[i] = SPATIAL_BASELINE_ALPHA * frame.mic[i].rms + (1.0f - SPATIAL_BASELINE_ALPHA) * micBaseline[i];
        }
        float safeBaseline = micBaseline[i] < SPATIAL_MIN_BASELINE ? SPATIAL_MIN_BASELINE : micBaseline[i];
        out.baseline[i] = safeBaseline;

        float norm = frame.mic[i].rms / safeBaseline;
        smoothedNorm[i] += (norm - smoothedNorm[i]) * SPATIAL_SMOOTHING_ALPHA;
        out.normalizedEnergy[i] = smoothedNorm[i];
    }

    // ---- Mode B input: calibrated RMS, single frame, no baseline division ----
    for (int i = 0; i < MIC_COUNT; i++) {
        out.calibratedRms[i] = frame.mic[i].rms;
    }

    // ---- Mode C input: calibrated RMS, short moving-average window ----
    for (int i = 0; i < MIC_COUNT; i++) {
        calRmsHistory[i][histIndex] = frame.mic[i].rms;
    }
    histIndex = (histIndex + 1) % SPATIAL_TEMPORAL_WINDOW_FRAMES;
    if (histFilled < SPATIAL_TEMPORAL_WINDOW_FRAMES) histFilled++;
    for (int i = 0; i < MIC_COUNT; i++) {
        float sum = 0.0f;
        for (int w = 0; w < histFilled; w++) sum += calRmsHistory[i][w];
        out.smoothedCalibratedRms[i] = sum / histFilled;
    }

    // ---- decide direction using whichever vector the active mode selects (PRODUCTION) ----
    const float *decisionVector = out.normalizedEnergy;
    if (currentMode == DirectionMode::CALIBRATED_RMS) decisionVector = out.calibratedRms;
    else if (currentMode == DirectionMode::SMOOTHED_CALIBRATED_RMS) decisionVector = out.smoothedCalibratedRms;

    DirectionDecision production = decideDirection(decisionVector, currentMode == DirectionMode::NORMALIZED_ENERGY);
    out.direction = production.direction;
    out.directionConfidence = production.confidence;
    out.spatialClass = production.spatialClass;
    out.dominanceMargin = production.dominanceMargin;
    out.leadingMic = production.leader;
    out.runnerUpMic = production.runnerUp;

    // ---- PHASE 2: per-band diagnostics, computed alongside but NOT fed
    // into the production fields above (see spatial_architecture_research.md Phase 4) ----
    for (int i = 0; i < MIC_COUNT; i++) {
        out.bandEnergy[BAND_LOW][i] = frame.mic[i].lowEnergy;
        out.bandEnergy[BAND_MID][i] = frame.mic[i].midEnergy;
        out.bandEnergy[BAND_HIGH][i] = frame.mic[i].highEnergy;
    }
    for (int b = 0; b < BAND_COUNT; b++) {
        DirectionDecision bandResult = decideDirection(out.bandEnergy[b], false);
        out.bandDirection[b] = bandResult.direction;
        out.bandConfidence[b] = bandResult.confidence;
    }

    // Fair baseline for comparison: calibrated RMS, no band split, no
    // baseline division -- same normalization treatment as the bands above,
    // so any difference is attributable to the band split itself.
    DirectionDecision broadband = decideDirection(out.calibratedRms, false);
    out.broadbandDirectionForCompare = broadband.direction;

    // Naive placeholder fusion -- see SpatialResult's comment. Not tuned,
    // not production, just a comparison baseline for Phase 3.
    int bestBand = BAND_LOW;
    if (out.bandConfidence[BAND_MID] > out.bandConfidence[bestBand]) bestBand = BAND_MID;
    if (out.bandConfidence[BAND_HIGH] > out.bandConfidence[bestBand]) bestBand = BAND_HIGH;
    out.fusedDirection = out.bandDirection[bestBand];
    out.fusedConfidence = out.bandConfidence[bestBand];

    return out;
}

AudioFrame buildAggregateFrame(const MicArrayFrame &frame, int leadingMic, int runnerUpMic) {
    AudioFrame out;
    const MicrophoneFrame &lead = frame.mic[leadingMic];
    const MicrophoneFrame &runner = frame.mic[runnerUpMic];

    // Combine EXCESS energy above each mic's own baseline, not raw RMS.
    // Raw summing (an earlier approach) added a mic's baseline-level "dead
    // weight" into both the signal and the reference for every frame, which
    // roughly HALVED the effective deviation ratio for a normal, clearly-
    // localized sound -- it only helped the rarer boundary-sound case, at a
    // steep cost to everything else. Using the leading mic's own baseline
    // as the reference reproduces exactly the single-mic math when the
    // runner-up is quiet (excess~0), while a genuine boundary sound still
    // gets real credit from both. UNCHANGED by Phase 1 -- event detection
    // still uses this exact aggregate regardless of DirectionMode.
    float leadBaseline = micBaseline[leadingMic] < SPATIAL_MIN_BASELINE ? SPATIAL_MIN_BASELINE : micBaseline[leadingMic];
    float runnerBaseline = micBaseline[runnerUpMic] < SPATIAL_MIN_BASELINE ? SPATIAL_MIN_BASELINE : micBaseline[runnerUpMic];

    float excessLead = lead.rms > leadBaseline ? (lead.rms - leadBaseline) : 0.0f;
    float excessRunner = runner.rms > runnerBaseline ? (runner.rms - runnerBaseline) : 0.0f;

    out.rms = leadBaseline + excessLead + excessRunner;
    out.avgAbs = lead.avgAbs;
    out.peak = lead.peak > runner.peak ? lead.peak : runner.peak;

    out.zcr = lead.zcr;
    out.highFreqRatio = lead.highFreqRatio;

    out.micOk = lead.micOk && runner.micOk;
    return out;
}
