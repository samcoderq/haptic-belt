#pragma once

// Dynamic Time Warping between two sequences of feature vectors (MFCC
// frames, mfcc_dsp.h) -- replaces the old fixed-window Euclidean distance
// in keyword_detector.cpp, which couldn't tolerate any timing misalignment
// between an enrolled template and a live utterance (documented as a known
// limitation from day one, and confirmed as the likely real cause once a
// controlled silence-vs-speech test showed the old approach couldn't
// separate the two at all). DTW time-aligns the two sequences instead of
// comparing them frame-for-frame at fixed positions, which is the standard
// technique for exactly this -- few-shot template matching where recordings
// of "the same word" are never exactly the same length or pace.

// Returns the length-normalized DTW cost (lower = more similar) between
// sequence a (aLen frames) and sequence b (bLen frames), each frame
// `dim` floats wide, laid out contiguously (frame i at a + i*dim). Local
// cost between two frames is plain Euclidean distance. aLen and bLen must
// each be <= DTW_MAX_FRAMES (see dtw.cpp) -- longer sequences are clamped
// there rather than overflowing the internal cost matrix.
float dtwDistance(const float *a, int aLen, const float *b, int bLen, int dim);
