#include "spatial_detector.h"
#include "config.h"
#include <math.h>

static float micBaseline[MIC_COUNT];
static float smoothedNorm[MIC_COUNT];
static bool  initialized = false;

void spatialDetectorInit() {
    for (int i = 0; i < MIC_COUNT; i++) {
        micBaseline[i] = 0.0f;
        smoothedNorm[i] = 0.0f;
    }
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

SpatialResult spatialDetectorUpdate(const MicArrayFrame &frame, bool freezeBaselines) {
    SpatialResult out;

    if (!initialized) {
        for (int i = 0; i < MIC_COUNT; i++) {
            micBaseline[i] = frame.mic[i].rms;
        }
        initialized = true;
    }

    for (int i = 0; i < MIC_COUNT; i++) {
        if (!freezeBaselines) {
            micBaseline[i] = SPATIAL_BASELINE_ALPHA * frame.mic[i].rms + (1.0f - SPATIAL_BASELINE_ALPHA) * micBaseline[i];
        }
        float safeBaseline = micBaseline[i] < SPATIAL_MIN_BASELINE ? SPATIAL_MIN_BASELINE : micBaseline[i];
        float norm = frame.mic[i].rms / safeBaseline;
        smoothedNorm[i] += (norm - smoothedNorm[i]) * SPATIAL_SMOOTHING_ALPHA;
        out.normalizedEnergy[i] = smoothedNorm[i];
    }

    // Rank mics by normalized energy, descending.
    int order[MIC_COUNT] = {0, 1, 2, 3};
    for (int i = 0; i < MIC_COUNT; i++) {
        for (int j = i + 1; j < MIC_COUNT; j++) {
            if (out.normalizedEnergy[order[j]] > out.normalizedEnergy[order[i]]) {
                int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
        }
    }
    int leader = order[0];
    int runnerUp = order[1];
    out.leadingMic = leader;
    out.runnerUpMic = runnerUp;

    float mean = 0.0f;
    for (int i = 0; i < MIC_COUNT; i++) mean += out.normalizedEnergy[i];
    mean /= MIC_COUNT;
    float variance = 0.0f;
    for (int i = 0; i < MIC_COUNT; i++) {
        float d = out.normalizedEnergy[i] - mean;
        variance += d * d;
    }
    variance /= MIC_COUNT;
    float cv = mean > 0.01f ? sqrtf(variance) / mean : 1.0f;

    if (mean >= GLOBAL_MEAN_THRESHOLD && cv <= GLOBAL_CV_THRESHOLD) {
        // Everything rose together -- do not claim a direction.
        out.direction = Direction::OMNIDIRECTIONAL;
        out.directionConfidence = 1.0f - (cv / GLOBAL_CV_THRESHOLD);
        out.spatialClass = SpatialClass::GLOBAL;
    } else {
        out.spatialClass = SpatialClass::LOCAL;
        float leaderVal = out.normalizedEnergy[leader];
        float runnerVal = out.normalizedEnergy[runnerUp];
        float margin = leaderVal > 0.01f ? (leaderVal - runnerVal) / leaderVal : 0.0f;

        if (margin >= DOMINANCE_MARGIN) {
            out.direction = singleDirection(leader);
            float confRange = DOMINANCE_MARGIN_FULL_CONF - DOMINANCE_MARGIN;
            float raw = confRange > 0 ? (margin - DOMINANCE_MARGIN) / confRange : 1.0f;
            out.directionConfidence = raw < 0.0f ? 0.0f : (raw > 1.0f ? 1.0f : raw);
        } else {
            out.direction = mergedDirection(leader, runnerUp);
            // Two adjacent mics agreeing closely is still real evidence, just
            // not a single point -- moderate confidence, not zero.
            out.directionConfidence = 0.3f + 0.4f * (margin / DOMINANCE_MARGIN);
            if (out.direction == Direction::UNKNOWN) {
                out.directionConfidence *= 0.5f;  // opposite-pair tie is genuinely more uncertain
            }
        }
    }

    return out;
}

AudioFrame buildAggregateFrame(const MicArrayFrame &frame, int leadingMic, int runnerUpMic) {
    AudioFrame out;
    const MicrophoneFrame &lead = frame.mic[leadingMic];
    const MicrophoneFrame &runner = frame.mic[runnerUpMic];

    // Sum top-2 amplitude so a sound split between two mics isn't
    // under-detected. A fully localized sound barely changes (runner-up
    // stays near its own baseline, contributing little).
    out.rms = lead.rms + runner.rms;
    out.avgAbs = lead.avgAbs + runner.avgAbs;
    out.peak = lead.peak > runner.peak ? lead.peak : runner.peak;

    // Timbre isn't additive the way energy is -- keep it from the dominant source.
    out.zcr = lead.zcr;
    out.highFreqRatio = lead.highFreqRatio;

    out.micOk = lead.micOk && runner.micOk;
    return out;
}
