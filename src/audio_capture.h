#pragma once

// audio_input module: owns the I2S peripheral and turns raw samples into
// per-frame features. Nothing above this layer touches I2S directly.
struct AudioFrame {
    float rms;
    float peak;
    float avgAbs;
    float zcr;            // zero-crossing rate, 0..1 (cheap timbre/impulsiveness cue)
    float highFreqRatio;  // 0..1, one-pole high-pass energy / raw energy (cheap spectral proxy, no FFT)
    bool  micOk;          // false if sustained zero-signal or clipping detected
};

bool audioInit();
bool audioReadFrame(AudioFrame &frame);
