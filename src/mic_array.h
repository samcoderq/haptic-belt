#pragma once
#include <stdint.h>

// mic_array module: raw 4-microphone acquisition across both I2S buses.
// This is deliberately separate from audio_capture.{h,cpp} (the working
// single-mic module) rather than a replacement -- audio_capture is left
// untouched per the checkpoint. Integration with event_detector happens
// in a later step, once all 4 channels are verified correct.

#define MIC_FRONT 0
#define MIC_RIGHT 1
#define MIC_BACK  2
#define MIC_LEFT  3
#define MIC_COUNT 4

struct MicrophoneFrame {
    float rms;       // calibrated (calibrationGain applied) -- unchanged meaning, existing callers unaffected
    float rawRms;    // pre-calibration RMS, for Phase 1 spatial diagnostics only
    float peak;
    float avgAbs;
    float zcr;
    float highFreqRatio;
    // PHASE 2: lightweight 3-band energy split (calibrated), for spatial
    // diagnostics only -- see config.h for the filter design and
    // docs/spatial_architecture_research.md Section 9/14 for why this was
    // worth trying (different bands sit in different near/far-field regimes
    // at the same real distance on our small aperture).
    float lowEnergy;   // approx <700Hz
    float midEnergy;   // approx 700-3000Hz
    float highEnergy;  // approx >3000Hz
    bool  micOk;
};

struct MicArrayFrame {
    MicrophoneFrame mic[MIC_COUNT];
};

bool micArrayInit();
bool micArrayReadFrame(MicArrayFrame &out);
const char* micName(int idx);

// Collects `numFrames` frames (call after the I2S warm-up frames, during a
// quiet moment) and computes a per-mic gain correction so all 4 mics report
// comparable levels for the same real ambient loudness -- corrects for the
// mics not having identical physical sensitivity, not for anything spatial.
// Applied automatically to rms/peak/avgAbs on every subsequent
// micArrayReadFrame() call. outGains/outAvgRms are for printing only.
void micArrayCalibrate(int numFrames, float outGains[MIC_COUNT], float outAvgRms[MIC_COUNT]);

// Extracts up to maxSamples de-interleaved, bit-shifted (same >>14 alignment
// as this file's own computeChannelStats) raw samples for micIdx from
// THIS frame's I2S read -- valid only until the next micArrayReadFrame()
// call overwrites the underlying buffer. Added for classifier_interface.cpp,
// which needs actual waveform data (not just the summary stats already in
// MicrophoneFrame) to tell sounds apart by their spectral shape. Returns the
// number of samples actually written (<= maxSamples).
int micArrayGetRawSamples(int micIdx, int32_t *outSamples, int maxSamples);
