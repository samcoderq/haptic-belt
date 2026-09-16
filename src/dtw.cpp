#include "dtw.h"
#include <math.h>

// Longest sequence this module will align -- must be >= the largest
// frameCount keyword_store.h will ever hand it (KEYWORD_MAX_TEMPLATE_FRAMES).
// A (DTW_MAX_FRAMES+1)^2 float matrix is static (not a stack local): the
// same class of bug hit twice already this project (keyword_detector.cpp's
// raw-sample scratch buffer, classifier_interface.cpp's block buffer) --
// both were oversized stack locals in a function called every loop() frame
// that overflowed the task stack. This one is smaller and called far less
// often (once per completed utterance, not every frame), but there's no
// reason to reintroduce the same risk a third time.
static const int DTW_MAX_FRAMES = 61;
static float costMatrix[DTW_MAX_FRAMES + 1][DTW_MAX_FRAMES + 1];

static float localDistance(const float *a, const float *b, int dim) {
    float sum = 0.0f;
    for (int i = 0; i < dim; i++) {
        float d = a[i] - b[i];
        sum += d * d;
    }
    return sqrtf(sum);
}

// Sakoe-Chiba band: the alignment path is only allowed to stray this many
// frames from the diagonal (|i-j| <= band). Added after a second opinion on
// this whole matching approach flagged that unconstrained DTW can produce
// pathological alignments -- warping so aggressively that it explains away
// genuinely different temporal structure as "the same word, just
// stretched," which works against the whole point of using DTW's alignment
// tolerance in the first place. `+6` frames (~144ms) of slack beyond the
// two sequences' own length difference is deliberately generous (real
// speaking-rate variation for a single short word is not usually large),
// not a tight fit -- this constrains obviously-wrong alignments without
// risking rejecting a legitimately slower/faster rendition of the same
// word.
static int sakoeChibaBand(int aLen, int bLen) {
    int diff = aLen > bLen ? aLen - bLen : bLen - aLen;
    return diff + 6;
}

float dtwDistance(const float *a, int aLen, const float *b, int bLen, int dim) {
    if (aLen > DTW_MAX_FRAMES) aLen = DTW_MAX_FRAMES;
    if (bLen > DTW_MAX_FRAMES) bLen = DTW_MAX_FRAMES;
    if (aLen <= 0 || bLen <= 0) return 1e9f;

    const float INF = 1e9f;
    const int band = sakoeChibaBand(aLen, bLen);
    costMatrix[0][0] = 0.0f;
    for (int i = 1; i <= aLen; i++) costMatrix[i][0] = INF;
    for (int j = 1; j <= bLen; j++) costMatrix[0][j] = INF;

    for (int i = 1; i <= aLen; i++) {
        const float *frameA = a + (size_t)(i - 1) * dim;
        int jLo = i - band > 1 ? i - band : 1;
        int jHi = i + band < bLen ? i + band : bLen;
        for (int j = 1; j < jLo; j++) costMatrix[i][j] = INF;
        for (int j = jHi + 1; j <= bLen; j++) costMatrix[i][j] = INF;
        for (int j = jLo; j <= jHi; j++) {
            const float *frameB = b + (size_t)(j - 1) * dim;
            float cost = localDistance(frameA, frameB, dim);
            float best = costMatrix[i - 1][j];
            if (costMatrix[i][j - 1] < best) best = costMatrix[i][j - 1];
            if (costMatrix[i - 1][j - 1] < best) best = costMatrix[i - 1][j - 1];
            costMatrix[i][j] = cost + best;
        }
    }

    // Path-length normalized so a short word and a long word's costs are
    // comparable on the same threshold instead of longer words always
    // scoring worse simply by summing more local-distance terms.
    return costMatrix[aLen][bLen] / (float)(aLen + bLen);
}
