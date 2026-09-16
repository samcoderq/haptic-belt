#include "keyword_detector.h"
#include "keyword_store.h"
#include "mfcc_dsp.h"
#include "dtw.h"
#include "mic_array.h"
#include "config.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>

// ---------------------------------------------------------------------
// Streaming MFCC frame extraction: a small sliding 512-sample raw-audio
// window (not a big ring buffer) that emits one MFCC frame every
// MFCC_FRAME_HOP (384) new samples. Static, not stack locals -- the same
// class of bug (an oversized stack-local in a function called every
// loop() frame overflowing the task stack) hit this exact file once
// already this project.
// ---------------------------------------------------------------------
static int16_t frameBuf[MFCC_FRAME_SIZE];
static int frameBufFilled = 0;

static int32_t rawSampleScratch[AUDIO_FRAME_SAMPLES * 2]; // headroom; a frame is usually <= AUDIO_FRAME_SAMPLES

// ---------------------------------------------------------------------
// Energy-based speech endpointing -- runs the DTW match only once a
// complete utterance has actually been detected and ended, instead of the
// old approach's unconditional check-every-window (which is what let it
// fire on silence just as readily as speech, per the controlled test that
// motivated this whole rewrite -- see keyword_detector.h). baselineEnergy
// only adapts while IDLE, same "freeze while active" principle
// environment_model.cpp already uses for its own baseline, for the same
// reason: don't let the thing you're detecting corrupt the model of what's
// normal.
// ---------------------------------------------------------------------
enum class SpeechState { IDLE, SPEAKING };
static SpeechState speechState = SpeechState::IDLE;
static float baselineEnergy = 0.0f;
static bool baselineInitialized = false;
static int lowEnergyStreak = 0;

static const float ENERGY_ON_RATIO    = 2.5f; // frame energy must exceed baseline*this to count as speech starting
static const float ENERGY_OFF_RATIO   = 1.5f; // frame energy must drop below baseline*this to count as silence again
static const int   ENDPOINT_HANG_FRAMES     = 8; // ~192ms of low energy before declaring the utterance ended
static const int   KEYWORD_MIN_TEMPLATE_FRAMES = 6;  // ~230ms; shorter than this is a blip, not a word -- discard
static const float BASELINE_ENERGY_ALPHA = 0.05f;
static const float MIN_BASELINE_ENERGY = 1e5f; // floor so ratios don't blow up near true silence

// Reverted 2.85->2.7 (2026-09-16, minutes after raising it): a live test
// at 2.85 produced real matches during a stretch where the user confirmed
// they were NOT saying the keyword -- genuine false positives, not just an
// unvalidated risk. 2.7 is the last value confirmed to NOT do this. See
// keyword_store.h and this project's whole 2026-09-16 session history for
// why "okay" specifically (short, phonetically generic) may be a poor
// choice of word for this algorithm regardless of threshold -- word choice
// matters as much as tuning for MFCC+DTW template matching.
static const float MAX_MATCH_DISTANCE = 2.7f;

// Required gap between the best-matching keyword's score and the runner-up
// KEYWORD's score (a different enrolled word, not another template of the
// same word) before accepting a match. Added after a second opinion on
// this whole approach identified the real bug behind "detection works but
// picks the wrong name": a single absolute threshold has no way to express
// "this is ambiguous" when two different enrolled words both happen to
// score close together for a given utterance (exactly what happened when
// this project had two similar keywords stored) -- it just picks whichever
// is a hair lower, with no signal that the win was razor-thin. A completely
// untested first-pass number: this project has never yet had a live test
// with two genuinely different (not accidentally near-duplicate) enrolled
// keywords, so there's no real data yet on what margin actually separates
// "confident" from "coin flip." With only one keyword enrolled there is no
// runner-up to compare against, so this check is skipped entirely.
static const float MIN_MARGIN = 0.15f;

static float utteranceBuffer[KEYWORD_TEMPLATE_MAX_FLOATS];
static int utteranceFrameCount = 0;

// ---------------------------------------------------------------------
// Frozen-leading-mic experiment (2026-09-15): spatial.leadingMic is
// recomputed fresh every ~100ms main-loop frame by spatial_detector.cpp
// and passed into keywordDetectorUpdate() unchanged -- an utterance
// spanning several calls could therefore have its raw audio pulled from a
// different physical mic almost every frame. This latches the mic that
// was actually being read at speech onset and keeps using it for the rest
// of that one utterance, while spatial.leadingMic itself keeps updating
// live and completely unaffected (spatial_detector.cpp is untouched --
// this only changes which mic keyword detection reads from). Controlled
// A/B: compare DTW score distributions with this latch active vs. the old
// always-follow-leadingMic behavior.
// ---------------------------------------------------------------------
static int latchedMic = -1;           // mic frozen for the current utterance; -1 = not speaking, track live
static int lastSeenLiveMic = -1;      // live leadingMic as of the previous call, for churn counting
static int leadingMicChangeCount = 0; // # times live leadingMic changed during the current utterance (diagnostic only)

static void resetUtterance() {
    speechState = SpeechState::IDLE;
    utteranceFrameCount = 0;
    lowEnergyStreak = 0;
    latchedMic = -1;
}

// Cepstral mean normalization: subtracts the utterance's own mean MFCC
// vector from every frame. Standard technique to reduce sensitivity to mic
// gain / distance-from-mic / recording-volume differences between an
// enrollment recording and a later live utterance -- applied identically
// here and in KeywordFeatures.kt's enrollment-side mirror, since a stored
// template and a live utterance both need it applied the same way to stay
// comparable.
static void applyCepstralMeanNormalization(float *frames, int frameCount) {
    float mean[MFCC_COUNT] = {0};
    for (int f = 0; f < frameCount; f++) {
        for (int c = 0; c < MFCC_COUNT; c++) mean[c] += frames[f * MFCC_COUNT + c];
    }
    for (int c = 0; c < MFCC_COUNT; c++) mean[c] /= frameCount;
    for (int f = 0; f < frameCount; f++) {
        for (int c = 0; c < MFCC_COUNT; c++) frames[f * MFCC_COUNT + c] -= mean[c];
    }
}

static KeywordMatch matchUtteranceAgainstStore() {
    KeywordMatch none = { false, nullptr, -1, 0.0f };
    if (utteranceFrameCount < KEYWORD_MIN_TEMPLATE_FRAMES) {
        // Diagnostic (2026-09-15): onset DID fire (we reached here at all,
        // via processFrame's offset path) but the utterance was too short
        // to bother matching -- previously silent, which made this
        // indistinguishable from onset never firing at all. Temporary,
        // additive-only debug print for the frozen-mic A/B investigation.
        Serial.print("Utterance too short to match: frames="); Serial.print(utteranceFrameCount);
        Serial.print(" (need >= "); Serial.print(KEYWORD_MIN_TEMPLATE_FRAMES); Serial.println(")");
        return none;
    }

    int keywordCount = keywordStoreCount();
    if (keywordCount == 0) return none;

    applyCepstralMeanNormalization(utteranceBuffer, utteranceFrameCount);

    // Per-keyword score = best (lowest) distance across THAT keyword's own
    // templates -- multiple templates per word are alternate pronunciations
    // of the SAME word (see keyword_store.h), not competitors, so within a
    // keyword we want its closest template, not an average or a penalty
    // for the others being farther. The margin check below then compares
    // across DIFFERENT keywords, which is where "ambiguous" actually means
    // something.
    int bestKeyword = -1;
    float bestDistance = 1e9f;
    int secondBestKeyword = -1;
    float secondBestDistance = 1e9f;

    for (int k = 0; k < keywordCount; k++) {
        int templateCount = keywordStoreTemplateCount(k);
        float keywordBest = 1e9f;
        for (int t = 0; t < templateCount; t++) {
            const float *stored = keywordStoreTemplate(k, t);
            int storedFrames = keywordStoreFrameCount(k, t);
            float dist = dtwDistance(utteranceBuffer, utteranceFrameCount, stored, storedFrames, MFCC_COUNT);
            if (dist < keywordBest) keywordBest = dist;
        }
        if (keywordBest < bestDistance) {
            secondBestDistance = bestDistance;
            secondBestKeyword = bestKeyword;
            bestDistance = keywordBest;
            bestKeyword = k;
        } else if (keywordBest < secondBestDistance) {
            secondBestDistance = keywordBest;
            secondBestKeyword = k;
        }
    }

    // Frozen-leading-mic experiment diagnostic (2026-09-15) -- printed once
    // per utterance, same as the existing "Keyword check" line below, so
    // the two can be read together per utterance during the A/B test.
    Serial.print("Utterance: startMic="); Serial.print(latchedMic);
    Serial.print(" leadingMicChanges="); Serial.print(leadingMicChangeCount);
    Serial.print(" frames="); Serial.print(utteranceFrameCount);
    Serial.print(" durationMs="); Serial.println((utteranceFrameCount * MFCC_FRAME_HOP * 1000) / AUDIO_SAMPLE_RATE);

    // Kept as a permanent diagnostic, not removed after tuning -- both
    // MAX_MATCH_DISTANCE and MIN_MARGIN needed real distance data to tune
    // meaningfully at all, not just a couple of live matches, and future
    // retuning will too.
    Serial.print("Keyword check: best='"); Serial.print(bestKeyword >= 0 ? keywordStoreName(bestKeyword) : "-");
    Serial.print("' distance="); Serial.print(bestDistance, 3);
    if (secondBestKeyword >= 0) {
        Serial.print(" | runner-up='"); Serial.print(keywordStoreName(secondBestKeyword));
        Serial.print("' distance="); Serial.print(secondBestDistance, 3);
        Serial.print(" margin="); Serial.println(secondBestDistance - bestDistance, 3);
    } else {
        Serial.println(" (only keyword enrolled, no margin check)");
    }

    if (bestKeyword < 0 || bestDistance > MAX_MATCH_DISTANCE) return none;
    if (secondBestKeyword >= 0 && (secondBestDistance - bestDistance) < MIN_MARGIN) {
        return none; // too close to the runner-up keyword to be confident -- ambiguous, not a match
    }

    KeywordMatch match;
    match.matched = true;
    match.name = keywordStoreName(bestKeyword);
    match.index = bestKeyword;
    match.distance = bestDistance;
    return match;
}

// Processes one newly-extracted MFCC frame through the endpointing state
// machine. Returns a real match only on the exact frame an utterance just
// ended and matched -- every other frame returns none, matching the
// existing one-shot-pulse contract main.cpp/ble_server.cpp already expect.
// Diagnostic-only (2026-09-15, frozen-mic A/B investigation): tracks the
// loudest IDLE-state frame energy seen since the last throttled print, so
// we can see whether real speech is even getting close to the onset
// threshold, or whether onset just never fires. Additive-only, does not
// affect detection behavior.
static float diagPeakEnergy = 0.0f;
static int diagFrameCounter = 0;
static const int DIAG_PRINT_EVERY = 40; // ~1s at ~24ms/frame

static KeywordMatch processFrame(const float *mfcc, float energy, int micUsedThisCall) {
    KeywordMatch none = { false, nullptr, -1, 0.0f };

    if (!baselineInitialized) {
        baselineEnergy = energy;
        baselineInitialized = true;
    }

    if (speechState == SpeechState::IDLE) {
        baselineEnergy = BASELINE_ENERGY_ALPHA * energy + (1.0f - BASELINE_ENERGY_ALPHA) * baselineEnergy;
        float safeBaseline = baselineEnergy < MIN_BASELINE_ENERGY ? MIN_BASELINE_ENERGY : baselineEnergy;

        if (energy > diagPeakEnergy) diagPeakEnergy = energy;
        diagFrameCounter++;
        if (diagFrameCounter >= DIAG_PRINT_EVERY) {
            Serial.print("Keyword idle: peakEnergy="); Serial.print(diagPeakEnergy, 1);
            Serial.print(" baseline="); Serial.print(safeBaseline, 1);
            Serial.print(" onsetThreshold="); Serial.println(safeBaseline * ENERGY_ON_RATIO, 1);
            diagPeakEnergy = 0.0f;
            diagFrameCounter = 0;
        }

        if (energy > safeBaseline * ENERGY_ON_RATIO) {
            speechState = SpeechState::SPEAKING;
            utteranceFrameCount = 0;
            lowEnergyStreak = 0;
            latchedMic = micUsedThisCall;   // freeze: this is the mic we'll keep reading for the whole utterance
            leadingMicChangeCount = 0;      // churn counter starts fresh for this utterance
            memcpy(utteranceBuffer + (size_t)utteranceFrameCount * MFCC_COUNT, mfcc, sizeof(float) * MFCC_COUNT);
            utteranceFrameCount++;
        }
        return none;
    }

    // SPEAKING
    float safeBaseline = baselineEnergy < MIN_BASELINE_ENERGY ? MIN_BASELINE_ENERGY : baselineEnergy;
    if (utteranceFrameCount < KEYWORD_MAX_TEMPLATE_FRAMES) {
        memcpy(utteranceBuffer + (size_t)utteranceFrameCount * MFCC_COUNT, mfcc, sizeof(float) * MFCC_COUNT);
        utteranceFrameCount++;
    }

    bool lowEnergy = energy < safeBaseline * ENERGY_OFF_RATIO;
    lowEnergyStreak = lowEnergy ? (lowEnergyStreak + 1) : 0;

    bool utteranceTooLong = utteranceFrameCount >= KEYWORD_MAX_TEMPLATE_FRAMES;
    if (lowEnergyStreak >= ENDPOINT_HANG_FRAMES || utteranceTooLong) {
        KeywordMatch result = matchUtteranceAgainstStore();
        resetUtterance();
        return result;
    }
    return none;
}

void keywordDetectorInit() {
    mfccInit((float)AUDIO_SAMPLE_RATE);
    frameBufFilled = 0;
    memset(frameBuf, 0, sizeof(frameBuf));
    resetUtterance();
    baselineInitialized = false;
    lastSeenLiveMic = -1;
    leadingMicChangeCount = 0;
}

KeywordMatch keywordDetectorUpdate(int leadingMic) {
    KeywordMatch none = { false, nullptr, -1, 0.0f };
    KeywordMatch result = none;

    // While mid-utterance, keep reading the mic that was latched at onset
    // instead of this call's fresh leadingMic argument -- the freeze itself.
    // spatial.leadingMic (main.cpp) keeps updating live regardless; only
    // read below, via lastSeenLiveMic, for the diagnostic churn count.
    int micToRead = (speechState == SpeechState::SPEAKING && latchedMic >= 0) ? latchedMic : leadingMic;

    if (speechState == SpeechState::SPEAKING && lastSeenLiveMic >= 0 && leadingMic != lastSeenLiveMic) {
        leadingMicChangeCount++;
    }
    lastSeenLiveMic = leadingMic;

    int n = micArrayGetRawSamples(micToRead, rawSampleScratch, (int)(sizeof(rawSampleScratch) / sizeof(rawSampleScratch[0])));

    if (keywordStoreCount() == 0) return none; // nothing enrolled -- skip the DSP work entirely

    for (int i = 0; i < n; i++) {
        int32_t s = rawSampleScratch[i];
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        frameBuf[frameBufFilled++] = (int16_t)s;

        if (frameBufFilled < MFCC_FRAME_SIZE) continue;

        float mfcc[MFCC_COUNT];
        float energy;
        mfccExtractFrame(frameBuf, mfcc, &energy);

        KeywordMatch frameResult = processFrame(mfcc, energy, micToRead);
        if (frameResult.matched) result = frameResult; // one-shot pulse for this call

        memmove(frameBuf, frameBuf + MFCC_FRAME_HOP, (MFCC_FRAME_SIZE - MFCC_FRAME_HOP) * sizeof(int16_t));
        frameBufFilled = MFCC_FRAME_SIZE - MFCC_FRAME_HOP;
    }

    return result;
}
