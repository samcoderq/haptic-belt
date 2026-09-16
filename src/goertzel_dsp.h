#pragma once
#include <stdint.h>

// Shared Goertzel DSP, used by both classifier_interface.cpp (single-block
// sound classification) and keyword_detector.cpp (multi-segment keyword
// template matching) -- one implementation, so there's no chance of the two
// drifting apart from each other or from the spec both must match
// (tools/goertzel.py, GoertzelFeatures.kt).

float goertzelPower(const int32_t *block, int n, float freq, float sampleRate);

// Computes one L1-normalized Goertzel feature vector over exactly blockSize
// samples at the given target frequencies (outFeatures must have room for
// featureCount floats). All-zero output for a near-silent block, matching
// the same convention as the Python/Kotlin mirrors.
void goertzelExtractFeatures(const int32_t *block, int blockSize,
                              const float *targetFreqs, int featureCount,
                              float sampleRate, float *outFeatures);
