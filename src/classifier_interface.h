#pragma once
#include "audio_capture.h"
#include "event_detector.h"

// classifier_interface module: real nearest-centroid classification over
// Goertzel frequency-bin energy features (see tools/goertzel.py for the
// exact spec this C code mirrors). Classes come from classifier_store,
// which the phone populates over BLE at runtime (Class Upload
// characteristic, ble_server.cpp) -- there is deliberately no compile-time
// model file and no reflash step: the phone computes a centroid from its
// own recorded samples (SoundTrainingScreen.kt) and uploads it directly.
//
// Starts with zero classes on a fresh board and always returns UNKNOWN
// until at least one has been uploaded, per the "don't fake capabilities"
// rule this project holds everywhere else. Even once trained, this is
// classical nearest-centroid matching, not a neural network -- deliberate,
// given the realistic sample counts (a handful of phone recordings per
// class, not thousands) a small neural net would be meaningless to call
// "trained" on. Nothing here has been tested against real recorded sounds
// or real hardware as of this writing.
struct ClassificationResult {
    const char* label;
    float confidence;
};

// leadingMic: which mic (MIC_FRONT/RIGHT/BACK/LEFT) to pull raw samples
// from for this frame's classification -- pass spatial.leadingMic, the same
// mic the rest of the pipeline already treats as the dominant source.
ClassificationResult classifyEvent(const AudioFrame &frame, const EventEvaluation &eval, int leadingMic);
