#include "goertzel_dsp.h"
#include <math.h>

float goertzelPower(const int32_t *block, int n, float freq, float sampleRate) {
    int k = (int)(n * freq / sampleRate + 0.5f);
    float w = 2.0f * (float)M_PI * (float)k / (float)n;
    float coeff = 2.0f * cosf(w);
    float sPrev = 0.0f, sPrev2 = 0.0f;
    for (int i = 0; i < n; i++) {
        float s = (float)block[i] + coeff * sPrev - sPrev2;
        sPrev2 = sPrev;
        sPrev = s;
    }
    return sPrev2 * sPrev2 + sPrev * sPrev - coeff * sPrev * sPrev2;
}

void goertzelExtractFeatures(const int32_t *block, int blockSize,
                              const float *targetFreqs, int featureCount,
                              float sampleRate, float *outFeatures) {
    float total = 0.0f;
    for (int b = 0; b < featureCount; b++) {
        float p = goertzelPower(block, blockSize, targetFreqs[b], sampleRate);
        outFeatures[b] = p;
        total += p;
    }
    if (total <= 1e-6f) {
        for (int b = 0; b < featureCount; b++) outFeatures[b] = 0.0f;
        return;
    }
    for (int b = 0; b < featureCount; b++) outFeatures[b] /= total;
}
