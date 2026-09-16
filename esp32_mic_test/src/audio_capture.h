#pragma once

// Minimal per-frame feature set -- exactly what environment_model.cpp and
// event_detector.cpp need. Mirrors the belt's original single-mic
// AudioFrame shape (that file no longer exists in the main project, since
// it was superseded by mic_array.h's 4-mic MicrophoneFrame; this is a
// trimmed-down standalone equivalent for this single-mic test board).
struct AudioFrame {
    float rms;
    float zcr;
    float highFreqRatio;
};
