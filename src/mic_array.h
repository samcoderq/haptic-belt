#pragma once

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
    float rms;
    float peak;
    float avgAbs;
    float zcr;
    float highFreqRatio;
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
