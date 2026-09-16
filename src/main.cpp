#include <Arduino.h>
#include "config.h"
#include "mic_array.h"
#include "spatial_detector.h"
#include "audio_capture.h"
#include "environment_model.h"
#include "event_detector.h"
#include "temporal_reasoner.h"
#include "priority_engine.h"
#include "classifier_interface.h"
#include "classifier_store.h"
#include "keyword_store.h"
#include "keyword_detector.h"
#include "direction_memory.h"
#include "awareness_output.h"
#include "ble_server.h"
#include "runtime_config.h"

// 4-mic spatial phase: direction comes from spatial_detector (relative
// energy across mics, NOT TDOA -- see spatial_detector.h). The existing,
// already-validated single-channel pipeline (environment_model ->
// event_detector -> temporal_reasoner -> priority_engine) is reused
// unmodified, fed by an "aggregate" AudioFrame built from whichever mic is
// currently loudest -- so a strongly localized sound is judged by its own
// coherent feature set, not a blended average across 4 mics.
//
// NOTE: BACK LED is on GPIO13, not GPIO11 -- GPIO11 was suspected bad and
// the wire was moved. See config.h (LED_BACK_PIN).

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("================================================");
    Serial.println("HAPTIC AWARENESS BELT");
    Serial.println("4-MIC SPATIAL + EVENT INTELLIGENCE");
    Serial.println("================================================");

    if (!micArrayInit()) {
        Serial.println("ERROR: I2S init failed on one or both buses. Check wiring/pins.");
        while (true) delay(1000);
    }

    MicArrayFrame warm;
    for (int i = 0; i < 3; i++) {
        micArrayReadFrame(warm);
    }

    Serial.println("Calibrating -- stay quiet for ~3 seconds...");
    float gains[MIC_COUNT];
    float avgRms[MIC_COUNT];
    micArrayCalibrate(30, gains, avgRms);
    Serial.println("Calibration results (mic-to-mic sensitivity correction, not direction):");
    for (int i = 0; i < MIC_COUNT; i++) {
        Serial.print(micName(i));
        Serial.print(": avgRMS="); Serial.print(avgRms[i], 1);
        Serial.print("  gain="); Serial.println(gains[i], 3);
    }

    spatialDetectorInit();
    environmentModelInit();
    eventDetectorInit();
    temporalReasonerInit();
    directionMemoryInit();
    awarenessOutputInit();
    classifierStoreInit();
    Serial.print("Classifier: "); Serial.print(classifierStoreClassCount());
    Serial.println(" class(es) loaded from flash.");
    keywordStoreInit();
    keywordDetectorInit();
    Serial.print("Keywords: "); Serial.print(keywordStoreCount());
    Serial.println(" keyword(s) loaded from flash.");
    bleServerInit();

    Serial.println("Listening...");
    Serial.println("BLE: advertising as 'Haptic Belt' (android_app/BLE_PROTOCOL.md) -- never tested against the real app yet.");
    Serial.println("Test commands: F/R/B/L/A/0 (single LED), 1-9/0 (direction pattern)");
    Serial.println("Direction mode: N=normalized-energy(default) C=calibrated-RMS S=smoothed-calibrated-RMS");
}

static unsigned long diagFrameCounter = 0;

// PHASE 1 diagnostic: which mic is loudest under each raw interpretation,
// independent of whichever mode is actually driving the real decision --
// this directly answers "does calibrated RMS stay right even when
// normalizedEnergy goes wrong."
static int argmax4(const float v[MIC_COUNT]) {
    int best = 0;
    for (int i = 1; i < MIC_COUNT; i++) if (v[i] > v[best]) best = i;
    return best;
}

void loop() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (handleTestCommand(c)) continue;
        if (c == 'N') { setDirectionMode(DirectionMode::NORMALIZED_ENERGY); Serial.println("Direction mode -> NORMALIZED_ENERGY"); }
        else if (c == 'C') { setDirectionMode(DirectionMode::CALIBRATED_RMS); Serial.println("Direction mode -> CALIBRATED_RMS"); }
        else if (c == 'S') { setDirectionMode(DirectionMode::SMOOTHED_CALIBRATED_RMS); Serial.println("Direction mode -> SMOOTHED_CALIBRATED_RMS"); }
        else if (c == 'V') {
            // Added while debugging why keyword matching was firing on
            // names the app's own enrolled-list no longer showed -- there
            // was no way to see what the belt itself actually has stored
            // (Keyword Upload is write-only, no BLE read-back exists).
            // ('V' not 'L' -- 'L' is already the LEFT test-LED command.)
            int n = keywordStoreCount();
            Serial.print("Keywords in flash ("); Serial.print(n); Serial.println("):");
            for (int i = 0; i < n; i++) {
                Serial.print("  ["); Serial.print(i); Serial.print("] "); Serial.println(keywordStoreName(i));
            }
        }
        else if (c == 'K') {
            keywordStoreClearAll();
            Serial.println("Keywords: cleared all.");
        }
    }

    // Applies whatever operating mode/sensitivity the phone last wrote to
    // the Settings characteristic (defaults if nothing written yet this
    // session/no phone ever connected) -- cheap enough to just redo every
    // frame rather than tracking whether it actually changed.
    BleSettings settings = bleServerGetSettings();
    applyRuntimeSensitivity((OperatingMode)settings.operatingMode, settings.sensitivity);

    MicArrayFrame frame;
    if (!micArrayReadFrame(frame)) {
        Serial.println("WARN: mic array read failed/timeout");
        return;
    }

    bool freeze = temporalReasonerIsActive();
    SpatialResult spatial = spatialDetectorUpdate(frame, freeze);

    // ---- PHASE 1 diagnostic block, throttled so Serial isn't flooded ----
    diagFrameCounter++;
    if (diagFrameCounter % SPATIAL_DIAG_PRINT_EVERY_N == 0) {
        Serial.println("=== SPATIAL DEBUG ===");
        Serial.print("MODE: "); Serial.println(directionModeName(getDirectionMode()));

        Serial.println("RAW:");
        for (int i = 0; i < MIC_COUNT; i++) {
            Serial.print(micName(i)); Serial.print("="); Serial.println(frame.mic[i].rawRms, 1);
        }
        Serial.println("CAL:");
        for (int i = 0; i < MIC_COUNT; i++) {
            Serial.print(micName(i)); Serial.print("="); Serial.println(spatial.calibratedRms[i], 1);
        }
        Serial.println("BASE:");
        for (int i = 0; i < MIC_COUNT; i++) {
            Serial.print(micName(i)); Serial.print("="); Serial.println(spatial.baseline[i], 1);
        }
        Serial.println("NORM:");
        for (int i = 0; i < MIC_COUNT; i++) {
            Serial.print(micName(i)); Serial.print("="); Serial.println(spatial.normalizedEnergy[i], 2);
        }
        Serial.println("SMOOTHED-CAL:");
        for (int i = 0; i < MIC_COUNT; i++) {
            Serial.print(micName(i)); Serial.print("="); Serial.println(spatial.smoothedCalibratedRms[i], 1);
        }

        // The core Phase 1 question: does the strongest mic under RAW/CAL
        // agree with NORM? If RAW/CAL agree with the true source direction
        // but NORM disagrees, adaptive-baseline normalization is the
        // likely culprit. If RAW/CAL already disagree, it isn't.
        float rawArr[MIC_COUNT];
        for (int i = 0; i < MIC_COUNT; i++) rawArr[i] = frame.mic[i].rawRms;
        Serial.print("ARGMAX  RAW="); Serial.print(micName(argmax4(rawArr)));
        Serial.print("  CAL="); Serial.print(micName(argmax4(spatial.calibratedRms)));
        Serial.print("  NORM="); Serial.println(micName(argmax4(spatial.normalizedEnergy)));

        Serial.print("STRONGEST: "); Serial.println(micName(spatial.leadingMic));
        Serial.print("RUNNER-UP: "); Serial.println(micName(spatial.runnerUpMic));
        Serial.print("MARGIN: "); Serial.println(spatial.dominanceMargin, 3);
        Serial.print("DIRECTION: "); Serial.println(directionName(spatial.direction));
        Serial.print("CONFIDENCE: "); Serial.println(spatial.directionConfidence, 2);
        Serial.println("=====================");

        // ---- PHASE 2: band-split spatial diagnostics ----
        Serial.println("=== BAND SPATIAL DEBUG ===");
        for (int i = 0; i < MIC_COUNT; i++) {
            Serial.print(micName(i)); Serial.println(":");
            Serial.print("LOW=");  Serial.print(spatial.bandEnergy[BAND_LOW][i], 1);
            Serial.print("  MID="); Serial.print(spatial.bandEnergy[BAND_MID][i], 1);
            Serial.print("  HIGH="); Serial.println(spatial.bandEnergy[BAND_HIGH][i], 1);
        }
        Serial.println();
        Serial.print("Broadband direction="); Serial.println(directionName(spatial.broadbandDirectionForCompare));
        Serial.print("Low-band direction=");  Serial.println(directionName(spatial.bandDirection[BAND_LOW]));
        Serial.print("Mid-band direction=");  Serial.println(directionName(spatial.bandDirection[BAND_MID]));
        Serial.print("High-band direction="); Serial.println(directionName(spatial.bandDirection[BAND_HIGH]));
        Serial.println();
        Serial.println("Band confidence (dominance-margin within that band this frame, NOT a measured reliability score):");
        Serial.print("LOW=");  Serial.println(spatial.bandConfidence[BAND_LOW], 2);
        Serial.print("MID=");  Serial.println(spatial.bandConfidence[BAND_MID], 2);
        Serial.print("HIGH="); Serial.println(spatial.bandConfidence[BAND_HIGH], 2);
        Serial.println();
        Serial.print("Fused direction=");  Serial.println(directionName(spatial.fusedDirection));
        Serial.print("Fused confidence="); Serial.println(spatial.fusedConfidence, 2);
        Serial.println("(Fused = naive highest-confidence-band placeholder, NOT tuned production fusion)");
        Serial.println("===========================");
    }

    AudioFrame agg = buildAggregateFrame(frame, spatial.leadingMic, spatial.runnerUpMic);
    EnvironmentState env = environmentModelUpdate(agg, freeze);
    EventEvaluation eval = evaluateEvent(env);
    TemporalResult temporal = temporalReasonerUpdate(eval.eventScore);

    int persistForPriority = (temporal.state == AwarenessState::NOTICE)
                                  ? temporal.noticePersistFrames
                                  : temporal.eventPersistFrames;

    bool isOnset = temporal.noticeAnnounce || temporal.eventAnnounce;
    DirectionMemoryResult dirMem = directionMemoryUpdate(spatial.direction, eval.eventScore, isOnset, temporal.noticeAnnounce);

    // Habituation only ever suppresses the OUTWARD announcement (LED flash,
    // serial log, BLE eventSeq bump) for a NOTICE onset -- temporal_reasoner's
    // own internal state/cooldown tracking above is untouched, and
    // CANDIDATE/EVENT onsets are never suppressed regardless of dirMem.habituated.
    bool noticeSuppressed = temporal.noticeAnnounce && dirMem.habituated;
    bool noticeAnnounceEffective = temporal.noticeAnnounce && !noticeSuppressed;
    bool isOnsetEffective = isOnset && !noticeSuppressed;

    Priority priority = evaluatePriority(eval.eventScore, eval.eventConfidence,
                                          dirMem.sameDirectionRepeatCount, persistForPriority,
                                          dirMem.isApproaching);
    ClassificationResult cls = classifyEvent(agg, eval, spatial.leadingMic);

    // Runs independent of the amplitude-based state machine above -- a
    // spoken name may not be loud enough to cross NOTICE/CANDIDATE
    // thresholds, so this can't be gated on temporal.state the way
    // classifyEvent() effectively is (via only being meaningful once
    // isOnset has fired). Computed here, before bleServerNotifyState(), so a
    // match reaches the phone in the same frame it fires rather than a frame
    // late.
    KeywordMatch keywordMatch = keywordDetectorUpdate(spatial.leadingMic);
    static uint8_t keywordMatchSlotLatched = 0;
    static unsigned long keywordMatchLatchUntilMs = 0;
    if (keywordMatch.matched) {
        Serial.print(">>> KEYWORD DETECTED: "); Serial.print(keywordMatch.name);
        Serial.print(" (distance="); Serial.print(keywordMatch.distance, 3);
        Serial.println(") <<<");
        // Real output now: a brief all-LED flash (triggerKeywordFlash(),
        // awareness_output.cpp), independent of the amplitude pipeline's
        // own pulse state so it can't corrupt whatever that's mid-way
        // through, PLUS this slot held in the BLE state packet's reserved
        // byte for KEYWORD_MATCH_BLE_LATCH_MS (config.h) -- see that
        // constant's comment for why this is latched, not a single-frame
        // pulse, as of 2026-09-16.
        triggerKeywordFlash();
        keywordMatchSlotLatched = (uint8_t)(keywordMatch.index + 1);
        keywordMatchLatchUntilMs = millis() + KEYWORD_MATCH_BLE_LATCH_MS;
    }
    uint8_t keywordMatchSlotForBle = (millis() < keywordMatchLatchUntilMs) ? keywordMatchSlotLatched : (uint8_t)0;

    updateOutput(spatial.direction, spatial.directionConfidence, temporal.state,
                 priority, temporal.eventAnnounce, noticeAnnounceEffective);

    bleServerNotifyState(spatial.direction, spatial.directionConfidence,
                          eval.eventScore, eval.eventConfidence,
                          temporal.state, priority,
                          dirMem.sameDirectionRepeatCount, dirMem.isApproaching, dirMem.isReceding,
                          cls.label, isOnsetEffective,
                          keywordMatchSlotForBle);
    uint16_t ackedSeq;
    if (bleServerConsumeAcknowledge(ackedSeq)) {
        // BLE_PROTOCOL.md Section 6: acknowledges the CRITICAL alert with
        // this eventSeq (already validated as matching the current
        // outstanding eventSeq inside ble_server.cpp's Ack callback).
        // Stops the continuous CRITICAL pulse immediately.
        Serial.print("BLE: Acknowledge received for eventSeq="); Serial.println(ackedSeq);
        acknowledgeCriticalAlert();
    }

    // Cheap (a bool check) when nothing's pending; does the actual blocking
    // NVS flash write when a class/keyword was just added -- deliberately
    // NOT done inside the BLE write callbacks themselves anymore, see
    // keyword_store.h's keywordStorePersistPending() for why.
    classifierStorePersistPending();
    keywordStorePersistPending();

    Serial.println("------------------------------------------------");
    for (int i = 0; i < MIC_COUNT; i++) {
        Serial.print(micName(i));
        Serial.print(" : RMS="); Serial.print(frame.mic[i].rms, 1);
        Serial.print("  PEAK="); Serial.print(frame.mic[i].peak, 1);
        Serial.print("  "); Serial.print(frame.mic[i].micOk ? "OK" : "CHECK INPUT");
        Serial.print("  NORM="); Serial.println(spatial.normalizedEnergy[i], 2);
    }
    Serial.println();
    Serial.println("EVENT:");
    Serial.print("Score       : "); Serial.println(eval.eventScore, 2);
    Serial.print("Confidence  : "); Serial.println(eval.eventConfidence, 2);
    Serial.print("State       : "); Serial.println(awarenessStateName(temporal.state));
    // Baseline confidence: how much the current fast/slow noise-floor model
    // is trusted right now (environment_model.cpp) -- this is what damps
    // eventScore in event_detector.cpp during a noisy/unstable stretch, so
    // it belongs right next to Score/State for anyone diagnosing why a noisy
    // room is or isn't triggering NOTICE/EVENT as often as expected.
    Serial.print("BaselineConf: "); Serial.print(env.baselineConfidence, 2);
    Serial.println(env.environmentChanging ? "  (environment changing)" : "");
    Serial.println();
    Serial.println("DIRECTION:");
    Serial.print("Direction   : "); Serial.println(directionName(spatial.direction));
    Serial.print("Confidence  : "); Serial.println(spatial.directionConfidence, 2);
    Serial.print("Spatial     : "); Serial.println(spatialClassName(spatial.spatialClass));
    Serial.println();
    Serial.println("PRIORITY:");
    Serial.println(priorityName(priority));
    Serial.print("Same-dir repeats: "); Serial.println(dirMem.sameDirectionRepeatCount);
    Serial.print("Trend: ");
    Serial.println(dirMem.isApproaching ? "APPROACHING" : (dirMem.isReceding ? "RECEDING" : "STABLE"));
    Serial.println();
    Serial.println("OUTPUT:");
    Serial.print("Direction LED: "); Serial.println(lastOutputDescription());
    Serial.print("Pattern: "); Serial.println(lastPatternDescription());
    Serial.print("Manual mode: "); Serial.println(isManualModeActive() ? "ON" : "off");
    Serial.println("------------------------------------------------");

    if (noticeAnnounceEffective) {
        Serial.print("NOTICE | score="); Serial.print(eval.eventScore, 2);
        Serial.print(" | direction="); Serial.print(directionName(spatial.direction));
        Serial.print(" | priority="); Serial.println(priorityName(priority));
    } else if (noticeSuppressed) {
        Serial.print("NOTICE suppressed (habituated, direction="); Serial.print(directionName(spatial.direction));
        Serial.println(")");
    }

    if (temporal.eventAnnounce) {
        Serial.println(">>> EVENT DETECTED <<<");
        Serial.print("Direction         : "); Serial.println(directionName(spatial.direction));
        Serial.print("Direction conf.   : "); Serial.println(spatial.directionConfidence, 2);
        Serial.print("Spatial           : "); Serial.println(spatialClassName(spatial.spatialClass));
        Serial.print("Event confidence  : "); Serial.println(eval.eventConfidence, 2);
        Serial.print("Classification    : "); Serial.println(cls.label);
        Serial.print("Same-dir repeats  : "); Serial.println(dirMem.sameDirectionRepeatCount);
        Serial.print("Trend             : ");
        Serial.println(dirMem.isApproaching ? "APPROACHING" : (dirMem.isReceding ? "RECEDING" : "STABLE"));
        Serial.print("Priority          : "); Serial.println(priorityName(priority));
    }

    // Driven by direction_memory (same-direction, matches what feeds
    // priority) instead of temporal_reasoner's old direction-agnostic
    // repeatAnnounce -- those two could previously disagree.
    if (isOnsetEffective && dirMem.sameDirectionRepeatCount >= REPEAT_COUNT_THRESHOLD) {
        Serial.println(">>> REPEATED ACTIVITY <<<");
        Serial.print("Direction: "); Serial.println(directionName(spatial.direction));
        Serial.print("Count: "); Serial.println(dirMem.sameDirectionRepeatCount);
    }
}
