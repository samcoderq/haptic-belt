#pragma once
#include <stdint.h>

// Shared MFCC (Mel-Frequency Cepstral Coefficient) feature extraction, used
// by keyword_detector.cpp's rewrite from Goertzel-power-ratio-bins +
// fixed-window Euclidean distance to MFCC + DTW.
//
// Why this replaced the Goertzel approach: a controlled real-hardware test
// (2026-09-15) measured pure silence and continuous real speech of an
// enrolled word separately and found their Goertzel-bin distances almost
// entirely overlapped (silence 0.78-1.34, speech 0.95-1.78) -- no threshold
// could separate them. MFCC (this file) + DTW (dtw.h) is the standard
// technique for exactly this project's situation -- few-shot custom keyword
// enrollment on embedded hardware, no training pipeline, borrowing the
// feature-extraction idea (not the trained-CNN classifier, which needs a
// labeled dataset this project doesn't have) from
// https://github.com/lbalic21/keyword_spotting, and confirmed as the
// standard classical approach for this exact few-shot/DTW use case by
// current research (e.g. "Multi-Sample Dynamic Time Warping for Few-Shot
// Keyword Spotting", EUSIPCO 2024).
//
// Parameters mirror common practice (20-40ms frames, 40 mel filters, 13
// coefficients, 80-7600Hz range for 16kHz audio) and specifically the
// referenced repo's own choices where reasonable, since there's no reason
// to deviate without data suggesting otherwise.

static const int MFCC_FRAME_SIZE  = 512;  // 32ms @ 16kHz, power-of-two for the FFT
static const int MFCC_FRAME_HOP   = 384;  // 24ms @ 16kHz
static const int MFCC_MEL_FILTERS = 40;
static const int MFCC_COUNT       = 13;
static const float MFCC_FREQ_MIN  = 80.0f;
static const float MFCC_FREQ_MAX  = 7600.0f; // must stay under Nyquist (8000 @ 16kHz)

// One-time setup of the mel filterbank bin boundaries for the given sample
// rate -- must be called once (mfccInit()) before mfccExtractFrame().
void mfccInit(float sampleRate);

// Extracts one MFCC_COUNT-length feature vector from exactly MFCC_FRAME_SIZE
// raw samples (outMfcc must have room for MFCC_COUNT floats). Applies a
// Hamming window, real FFT, mel filterbank, log, and DCT-II -- the standard
// MFCC pipeline. outFrameEnergy (optional, pass nullptr to skip) receives
// the raw (pre-window) frame energy, used by keyword_detector.cpp for
// energy-based speech endpointing without a second pass over the samples.
void mfccExtractFrame(const int16_t *samples, float *outMfcc, float *outFrameEnergy);
