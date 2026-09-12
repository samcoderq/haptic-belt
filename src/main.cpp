#include <Arduino.h>
#include "config.h"
#include "audio_capture.h"
#include "environment_model.h"
#include "event_detector.h"
#include "temporal_reasoner.h"
#include "priority_engine.h"
#include "classifier_interface.h"

static unsigned long lastPerfPrint = 0;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("================================================");
    Serial.println("HAPTIC AWARENESS BELT");
    Serial.println("SINGLE MIC INTELLIGENCE TEST");
    Serial.println("================================================");

    if (!audioInit()) {
        Serial.println("ERROR: I2S init failed. Check wiring/pins.");
        while (true) delay(1000);
    }
    environmentModelInit();
    eventDetectorInit();
    temporalReasonerInit();

    // The very first I2S read after reset can contain a startup pop/garbage
    // transient (observed on this board/module during earlier testing).
    // Discard a few frames before anything seeds off real data.
    AudioFrame warmupFrame;
    for (int i = 0; i < 3; i++) {
        audioReadFrame(warmupFrame);
    }

    Serial.println("I2S initialized. Listening...");
    Serial.print("Buffer: "); Serial.print(AUDIO_FRAME_SAMPLES);
    Serial.print(" samples @ "); Serial.print(AUDIO_SAMPLE_RATE); Serial.println(" Hz");
}

void loop() {
    AudioFrame frame;
    if (!audioReadFrame(frame)) {
        Serial.println("WARN: audio read failed/timeout");
        return;
    }

    unsigned long computeStart = micros();

    bool freeze = temporalReasonerIsActive();
    EnvironmentState env = environmentModelUpdate(frame, freeze);
    EventEvaluation eval = evaluateEvent(env);
    TemporalResult temporal = temporalReasonerUpdate(eval.eventScore);

    // Persistence is what should drive "is this worth the user's attention",
    // not raw loudness (a single mic can't tell distant-and-important apart
    // from nearby-and-trivial by amplitude alone -- see NOTICE handling).
    int persistForPriority = (temporal.state == AwarenessState::NOTICE)
                                  ? temporal.noticePersistFrames
                                  : temporal.eventPersistFrames;
    Priority priority = evaluatePriority(eval.eventScore, eval.eventConfidence,
                                          temporal.repeatCount, persistForPriority);
    ClassificationResult cls = classifyEvent(frame, eval);

    unsigned long computeMicros = micros() - computeStart;

#if !COMPACT_OUTPUT
    Serial.println("------------------------------------------------");
    Serial.println("MIC:");
    Serial.print("RMS: "); Serial.println(frame.rms, 1);
    Serial.print("PEAK: "); Serial.println(frame.peak, 1);
    Serial.print("MIC STATUS: "); Serial.println(frame.micOk ? "OK" : "CHECK INPUT");
    Serial.println();
    Serial.println("ENVIRONMENT:");
    Serial.print("Baseline: "); Serial.println(env.fastBaseline, 1);
    Serial.print("SlowBaseline: "); Serial.println(env.slowBaseline, 1);
    Serial.print("Confidence: "); Serial.println(env.baselineConfidence, 2);
    Serial.println();
    Serial.println("DETECTION:");
    Serial.print("Deviation: "); Serial.println(env.energyDeviation, 2);
    Serial.print("Event Score: "); Serial.println(eval.eventScore, 2);
    Serial.print("Event Confidence: "); Serial.println(eval.eventConfidence, 2);
    Serial.print("State: "); Serial.println(awarenessStateName(temporal.state));
    Serial.println();
    Serial.println("INTELLIGENCE:");
    Serial.print("Class: "); Serial.println(cls.label);
    Serial.print("ClassConf: "); Serial.println(cls.confidence, 2);
    Serial.print("Priority: "); Serial.println(priorityName(priority));
    Serial.print("Repeats: "); Serial.println(temporal.repeatCount);
    Serial.println("------------------------------------------------");
#else
    Serial.print("RMS="); Serial.print(frame.rms, 0);
    Serial.print(" BASE="); Serial.print(env.fastBaseline, 0);
    Serial.print(" CONF="); Serial.print(env.baselineConfidence, 2);
    Serial.print(" SCORE="); Serial.print(eval.eventScore, 2);
    Serial.print(" STATE="); Serial.println(awarenessStateName(temporal.state));
#endif

    if (env.justChanged) {
        Serial.println(">>> ENVIRONMENT CHANGE <<<");
        Serial.println("Baseline adapting...");
    }
    if (env.justStabilized) {
        Serial.print("Environment stabilized. Baseline confidence: ");
        Serial.println(env.baselineConfidence, 2);
    }

    if (temporal.noticeAnnounce) {
        Serial.print("NOTICE | score="); Serial.print(eval.eventScore, 2);
        Serial.print(" | baseline_conf="); Serial.print(env.baselineConfidence, 2);
        Serial.print(" | priority="); Serial.println(priorityName(priority));
    }

    if (temporal.eventAnnounce) {
        Serial.println(">>> EVENT DETECTED <<<");
        Serial.print("Event score       : "); Serial.println(eval.eventScore, 2);
        Serial.print("Event confidence  : "); Serial.println(eval.eventConfidence, 2);
        Serial.print("Classification    : "); Serial.println(cls.label);
        Serial.print("Classification C. : "); Serial.println(cls.confidence, 2);
        Serial.print("Priority          : "); Serial.println(priorityName(priority));
    }

    if (temporal.repeatAnnounce) {
        Serial.println(">>> REPEATED ACTIVITY <<<");
        Serial.print("Count: "); Serial.println(temporal.repeatCount);
        Serial.print("Pattern: "); Serial.println(temporal.pattern);
    }

    if (!frame.micOk) {
        Serial.println("MIC STATUS: CHECK INPUT");
    }

    if (millis() - lastPerfPrint > 5000) {
        lastPerfPrint = millis();
        Serial.print("[perf] compute="); Serial.print(computeMicros);
        Serial.print("us  frame_interval=~100ms  free_heap=");
        Serial.print(ESP.getFreeHeap());
        Serial.println(" bytes");
    }
}
