#include "mfcc_dsp.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------
// Radix-2 Cooley-Tukey FFT, iterative, in-place. MFCC_FRAME_SIZE (512) is
// already a power of two so no padding is needed. Twiddle factors are
// precomputed once in mfccInit() rather than recomputed per-frame -- this
// runs every MFCC_FRAME_HOP (24ms) while capturing an utterance, frequent
// enough that repeated cosf/sinf calls would be wasteful.
// ---------------------------------------------------------------------
static float twiddleCos[MFCC_FRAME_SIZE / 2];
static float twiddleSin[MFCC_FRAME_SIZE / 2];
static int bitReverseTable[MFCC_FRAME_SIZE];

static int log2i(int n) {
    int r = 0;
    while (n > 1) { n >>= 1; r++; }
    return r;
}

static void fft(float *re, float *im) {
    const int n = MFCC_FRAME_SIZE;

    for (int i = 0; i < n; i++) {
        int j = bitReverseTable[i];
        if (j > i) {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }

    for (int size = 2; size <= n; size <<= 1) {
        int half = size / 2;
        int step = n / size;
        for (int start = 0; start < n; start += size) {
            for (int k = 0; k < half; k++) {
                int twIdx = k * step;
                float wr = twiddleCos[twIdx];
                float wi = -twiddleSin[twIdx]; // forward transform: e^-j*theta
                int a = start + k;
                int b = a + half;
                float br = re[b] * wr - im[b] * wi;
                float bi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - br;
                im[b] = im[a] - bi;
                re[a] = re[a] + br;
                im[a] = im[a] + bi;
            }
        }
    }
}

// ---------------------------------------------------------------------
// Mel filterbank: 40 triangular filters spanning MFCC_FREQ_MIN..MAX, evenly
// spaced on the mel scale (standard recipe -- see e.g. any MFCC reference).
// Stored as (left, center, right) FFT bin indices per filter, computed once
// in mfccInit(), not the full dense 40 x 257 weight matrix -- each triangle
// only touches a handful of bins, so this is both far less memory and lets
// mfccExtractFrame() skip straight to the bins that matter.
// ---------------------------------------------------------------------
static int melBinLeft[MFCC_MEL_FILTERS];
static int melBinCenter[MFCC_MEL_FILTERS];
static int melBinRight[MFCC_MEL_FILTERS];

static float hzToMel(float hz) { return 2595.0f * log10f(1.0f + hz / 700.0f); }
static float melToHz(float mel) { return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f); }

void mfccInit(float sampleRate) {
    for (int i = 0; i < MFCC_FRAME_SIZE; i++) {
        int rev = 0;
        int x = i;
        for (int b = 0; b < log2i(MFCC_FRAME_SIZE); b++) {
            rev = (rev << 1) | (x & 1);
            x >>= 1;
        }
        bitReverseTable[i] = rev;
    }
    for (int k = 0; k < MFCC_FRAME_SIZE / 2; k++) {
        float theta = 2.0f * (float)M_PI * k / MFCC_FRAME_SIZE;
        twiddleCos[k] = cosf(theta);
        twiddleSin[k] = sinf(theta);
    }

    float melMin = hzToMel(MFCC_FREQ_MIN);
    float melMax = hzToMel(MFCC_FREQ_MAX);
    float melStep = (melMax - melMin) / (MFCC_MEL_FILTERS + 1);

    int points[MFCC_MEL_FILTERS + 2];
    for (int i = 0; i < MFCC_MEL_FILTERS + 2; i++) {
        float hz = melToHz(melMin + melStep * i);
        int bin = (int)roundf(hz * MFCC_FRAME_SIZE / sampleRate);
        if (bin < 0) bin = 0;
        if (bin > MFCC_FRAME_SIZE / 2) bin = MFCC_FRAME_SIZE / 2;
        points[i] = bin;
    }
    for (int m = 0; m < MFCC_MEL_FILTERS; m++) {
        melBinLeft[m]   = points[m];
        melBinCenter[m] = points[m + 1];
        melBinRight[m]  = points[m + 2];
    }
}

void mfccExtractFrame(const int16_t *samples, float *outMfcc, float *outFrameEnergy) {
    static float re[MFCC_FRAME_SIZE];
    static float im[MFCC_FRAME_SIZE];
    static float magnitude[MFCC_FRAME_SIZE / 2 + 1];
    static float melEnergy[MFCC_MEL_FILTERS];

    if (outFrameEnergy != nullptr) {
        float energy = 0.0f;
        for (int i = 0; i < MFCC_FRAME_SIZE; i++) energy += (float)samples[i] * (float)samples[i];
        *outFrameEnergy = energy / MFCC_FRAME_SIZE;
    }

    // Hamming window, standard MFCC pre-processing to reduce spectral
    // leakage from framing a continuous signal into fixed blocks.
    for (int i = 0; i < MFCC_FRAME_SIZE; i++) {
        float w = 0.54f - 0.46f * cosf(2.0f * (float)M_PI * i / (MFCC_FRAME_SIZE - 1));
        re[i] = (float)samples[i] * w;
        im[i] = 0.0f;
    }

    fft(re, im);

    for (int k = 0; k <= MFCC_FRAME_SIZE / 2; k++) {
        magnitude[k] = sqrtf(re[k] * re[k] + im[k] * im[k]);
    }

    for (int m = 0; m < MFCC_MEL_FILTERS; m++) {
        int left = melBinLeft[m], center = melBinCenter[m], right = melBinRight[m];
        float sum = 0.0f;
        for (int k = left; k < center; k++) {
            float w = (center > left) ? (float)(k - left) / (center - left) : 0.0f;
            sum += magnitude[k] * w;
        }
        for (int k = center; k < right; k++) {
            float w = (right > center) ? (float)(right - k) / (right - center) : 0.0f;
            sum += magnitude[k] * w;
        }
        // Floor avoids log(0) for a silent/near-silent filter band.
        melEnergy[m] = log10f(sum + 1e-6f);
    }

    // DCT-II, standard formula, keeping coefficients C1..C(MFCC_COUNT) --
    // C0 (k=0) is deliberately dropped: it's essentially the overall
    // log-energy of the frame, which mostly reflects loudness/mic
    // gain/distance rather than phonetic content, and would dominate a
    // Euclidean/DTW distance between an enrollment recording and a later
    // live utterance said at a different volume or distance from the mic.
    // Standard practice in MFCC-based speech matching, not specific to this
    // project. outMfcc[0] here is C1, not C0.
    for (int k = 1; k <= MFCC_COUNT; k++) {
        float sum = 0.0f;
        for (int m = 0; m < MFCC_MEL_FILTERS; m++) {
            sum += melEnergy[m] * cosf((float)M_PI / MFCC_MEL_FILTERS * (m + 0.5f) * k);
        }
        outMfcc[k - 1] = sum;
    }
}
