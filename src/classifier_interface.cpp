#include "classifier_interface.h"
#include "classifier_store.h"
#include "goertzel_dsp.h"
#include "mic_array.h"
#include "config.h"
#include <math.h>

// Minimum cosine similarity to the nearest centroid before committing to a
// label instead of UNKNOWN -- a first-pass guess, not tuned against real
// data (there is none yet; see classifier_store.h).
static const float CLASSIFIER_MIN_SIMILARITY = 0.75f;

// Mirrors tools/goertzel.py's extract_features(): zero-pads to exactly
// CLASSIFIER_BLOCK_SIZE (matching the training-side behavior for a short
// clip) before running Goertzel (goertzel_dsp.h -- shared with
// keyword_detector.cpp), same target frequencies, same L1 normalization.
// Static, not stack locals: nested 4KB+4KB stack buffers here are the same
// class of bug that crashed keyword_detector.cpp's loop-task stack the first
// time this ran on real hardware (see rawSampleScratch there). This path was
// dormant during that crash only because classCount was 0 (nothing trained
// yet) -- it would have hit the same overflow the first time a class got
// trained and a sound was classified.
static int32_t extractBlockScratch[CLASSIFIER_BLOCK_SIZE];

static void extractFeatures(const int32_t *rawSamples, int n, float outFeatures[CLASSIFIER_FEATURE_COUNT]) {
    for (int i = 0; i < CLASSIFIER_BLOCK_SIZE; i++) {
        extractBlockScratch[i] = (i < n) ? rawSamples[i] : 0;
    }
    goertzelExtractFeatures(extractBlockScratch, CLASSIFIER_BLOCK_SIZE, CLASSIFIER_TARGET_FREQS,
                             CLASSIFIER_FEATURE_COUNT, (float)AUDIO_SAMPLE_RATE, outFeatures);
}

static float cosineSimilarity(const float a[CLASSIFIER_FEATURE_COUNT], const float b[CLASSIFIER_FEATURE_COUNT]) {
    float dot = 0.0f, na = 0.0f, nb = 0.0f;
    for (int i = 0; i < CLASSIFIER_FEATURE_COUNT; i++) {
        dot += a[i] * b[i];
        na += a[i] * a[i];
        nb += b[i] * b[i];
    }
    if (na <= 1e-9f || nb <= 1e-9f) return 0.0f;
    return dot / (sqrtf(na) * sqrtf(nb));
}

ClassificationResult classifyEvent(const AudioFrame &frame, const EventEvaluation &eval, int leadingMic) {
    ClassificationResult unknown = { "UNKNOWN", 0.0f };

    int classCount = classifierStoreClassCount();
    if (classCount == 0) {
        // Nothing uploaded from the phone yet this board's lifetime -- see
        // classifier_store.h.
        return unknown;
    }

    static int32_t rawSampleScratch[CLASSIFIER_BLOCK_SIZE];
    int n = micArrayGetRawSamples(leadingMic, rawSampleScratch, CLASSIFIER_BLOCK_SIZE);
    if (n < CLASSIFIER_BLOCK_SIZE / 2) {
        // Short read this frame -- rather than classify off a mostly-padded
        // (mostly-zero) block and risk a misleading match, just say UNKNOWN.
        return unknown;
    }

    float features[CLASSIFIER_FEATURE_COUNT];
    extractFeatures(rawSampleScratch, n, features);

    int bestClass = -1;
    float bestSimilarity = -1.0f;
    for (int c = 0; c < classCount; c++) {
        const float *centroid = classifierStoreCentroid(c);
        float sim = cosineSimilarity(features, centroid);
        if (sim > bestSimilarity) {
            bestSimilarity = sim;
            bestClass = c;
        }
    }

    if (bestClass < 0 || bestSimilarity < CLASSIFIER_MIN_SIMILARITY) {
        return unknown;
    }

    ClassificationResult out;
    out.label = classifierStoreClassName(bestClass);
    out.confidence = bestSimilarity;
    return out;
}
