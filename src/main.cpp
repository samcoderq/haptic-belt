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
#include "direction_memory.h"
#include "awareness_output.h"

// 4-mic spatial phase: direction comes from spatial_detector (relative
// energy across mics, NOT TDOA -- see spatial_detector.h). The existing,
// already-validated single-channel pipeline (environment_model ->
// event_detector -> temporal_reasoner -> priority_engine) is reused
// unmodified, fed by an "aggregate" AudioFrame built from whichever mic is
// currently loudest -- so a strongly localized sound is judged by its own
// coherent feature set, not a blended average across 4 mics.

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

    Serial.println("Listening...");
    Serial.println("Test commands: F/R/B/L/A/0 (single LED), 1-9/0 (direction pattern)");
}

void loop() {
    while (Serial.available()) {
        handleTestCommand((char)Serial.read());
    }

    MicArrayFrame frame;
    if (!micArrayReadFrame(frame)) {
        Serial.println("WARN: mic array read failed/timeout");
        return;
    }

    bool freeze = temporalReasonerIsActive();
    SpatialResult spatial = spatialDetectorUpdate(frame, freeze);

    AudioFrame agg = buildAggregateFrame(frame, spatial.leadingMic, spatial.runnerUpMic);
    EnvironmentState env = environmentModelUpdate(agg, freeze);
    EventEvaluation eval = evaluateEvent(env);
    TemporalResult temporal = temporalReasonerUpdate(eval.eventScore);

    int persistForPriority = (temporal.state == AwarenessState::NOTICE)
                                  ? temporal.noticePersistFrames
                                  : temporal.eventPersistFrames;

    bool isOnset = temporal.noticeAnnounce || temporal.eventAnnounce;
    DirectionMemoryResult dirMem = directionMemoryUpdate(spatial.direction, eval.eventScore, isOnset);

    Priority priority = evaluatePriority(eval.eventScore, eval.eventConfidence,
                                          dirMem.sameDirectionRepeatCount, persistForPriority,
                                          dirMem.isApproaching);
    ClassificationResult cls = classifyEvent(agg, eval);

    updateOutput(spatial.direction, spatial.directionConfidence, temporal.state,
                 priority, temporal.eventAnnounce, temporal.noticeAnnounce);

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

    if (temporal.noticeAnnounce) {
        Serial.print("NOTICE | score="); Serial.print(eval.eventScore, 2);
        Serial.print(" | direction="); Serial.print(directionName(spatial.direction));
        Serial.print(" | priority="); Serial.println(priorityName(priority));
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
    if (isOnset && dirMem.sameDirectionRepeatCount >= REPEAT_COUNT_THRESHOLD) {
        Serial.println(">>> REPEATED ACTIVITY <<<");
        Serial.print("Direction: "); Serial.println(directionName(spatial.direction));
        Serial.print("Count: "); Serial.println(dirMem.sameDirectionRepeatCount);
    }
}
