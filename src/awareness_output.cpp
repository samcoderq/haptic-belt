#include "awareness_output.h"
#include "config.h"
#include <Arduino.h>
#include <string.h>

static int ledPins[MIC_COUNT];

static bool manualMode = false;

enum class PulseMode { NONE, FIXED_COUNT, CONTINUOUS };
static PulseMode pulseMode = PulseMode::NONE;
static int pulsesRemaining = 0;
static bool pulseOn = false;
static unsigned long lastToggleMs = 0;

static bool unknownFlashActive = false;
static unsigned long unknownFlashStartMs = 0;

static bool keywordFlashActive = false;
static unsigned long keywordFlashStartMs = 0;

static bool criticalAcknowledged = false;

static const char* lastOutputDesc = "OFF";
static const char* lastPatternDesc = "NONE";

// ---- helpers ----

static void ledsForDirection(Direction d, bool out[MIC_COUNT]) {
    out[MIC_FRONT] = out[MIC_RIGHT] = out[MIC_BACK] = out[MIC_LEFT] = false;
    switch (d) {
        case Direction::FRONT: out[MIC_FRONT] = true; break;
        case Direction::RIGHT: out[MIC_RIGHT] = true; break;
        case Direction::BACK:  out[MIC_BACK]  = true; break;
        case Direction::LEFT:  out[MIC_LEFT]  = true; break;
        case Direction::FRONT_RIGHT: out[MIC_FRONT] = true; out[MIC_RIGHT] = true; break;
        case Direction::RIGHT_BACK:  out[MIC_RIGHT] = true; out[MIC_BACK]  = true; break;
        case Direction::BACK_LEFT:   out[MIC_BACK]  = true; out[MIC_LEFT]  = true; break;
        case Direction::LEFT_FRONT:  out[MIC_LEFT]  = true; out[MIC_FRONT] = true; break;
        case Direction::OMNIDIRECTIONAL:
            out[MIC_FRONT] = out[MIC_RIGHT] = out[MIC_BACK] = out[MIC_LEFT] = true;
            break;
        case Direction::UNKNOWN:
        default:
            break;
    }
}

static int brightnessForConfidence(float conf) {
    if (conf < 0.30f) return LED_PWM_VERY_LOW;
    if (conf < 0.50f) return LED_PWM_DIM;
    if (conf < 0.75f) return LED_PWM_MEDIUM;
    return LED_PWM_BRIGHT;
}

static void writeLeds(const bool active[MIC_COUNT], int brightness) {
    for (int i = 0; i < MIC_COUNT; i++) {
        analogWrite(ledPins[i], active[i] ? brightness : 0);
    }
}

static const char* directionDescription(const bool active[MIC_COUNT]) {
    int count = 0;
    for (int i = 0; i < MIC_COUNT; i++) if (active[i]) count++;
    if (count == 0) return "OFF";
    if (count == 4) return "ALL";
    if (count == 1) {
        if (active[MIC_FRONT]) return "FRONT";
        if (active[MIC_RIGHT]) return "RIGHT";
        if (active[MIC_BACK])  return "BACK";
        return "LEFT";
    }
    if (active[MIC_FRONT] && active[MIC_RIGHT]) return "FRONT+RIGHT";
    if (active[MIC_RIGHT] && active[MIC_BACK])  return "RIGHT+BACK";
    if (active[MIC_BACK]  && active[MIC_LEFT])  return "BACK+LEFT";
    return "LEFT+FRONT";
}

static const char* priorityDescription(Priority p) {
    switch (p) {
        case Priority::PRIORITY_LOW:      return "LOW";
        case Priority::PRIORITY_MEDIUM:   return "MEDIUM";
        case Priority::PRIORITY_HIGH:     return "HIGH";
        case Priority::PRIORITY_CRITICAL: return "CRITICAL";
    }
    return "?";
}

static int pulseCountForPriority(Priority p) {
    switch (p) {
        case Priority::PRIORITY_LOW:      return 1;
        case Priority::PRIORITY_MEDIUM:   return 2;
        case Priority::PRIORITY_HIGH:     return 3;
        case Priority::PRIORITY_CRITICAL: return -1;  // continuous, not a fixed count
    }
    return 1;
}

// ---- public API ----

void awarenessOutputInit() {
    ledPins[MIC_FRONT] = LED_FRONT_PIN;
    ledPins[MIC_RIGHT] = LED_RIGHT_PIN;
    ledPins[MIC_BACK]  = LED_BACK_PIN;
    ledPins[MIC_LEFT]  = LED_LEFT_PIN;
    for (int i = 0; i < MIC_COUNT; i++) {
        pinMode(ledPins[i], OUTPUT);
        analogWrite(ledPins[i], 0);
    }
}

void allOutputsOff() {
    bool none[MIC_COUNT] = {false, false, false, false};
    writeLeds(none, 0);
    pulseMode = PulseMode::NONE;
    unknownFlashActive = false;
    lastOutputDesc = "OFF";
    lastPatternDesc = "NONE";
}

void updateOutput(Direction direction, float directionConfidence, AwarenessState state,
                   Priority priority, bool eventJustAnnounced, bool noticeJustAnnounced) {
    if (manualMode) {
        return;  // manual test owns the LEDs until '0' clears it
    }

    unsigned long now = millis();

    // Keyword match overlay -- checked before anything amplitude-pipeline-
    // related so it's visible regardless of current state (including
    // BACKGROUND, which would otherwise allOutputsOff() before this ever
    // ran). Falls through to normal behavior once the flash duration ends.
    if (keywordFlashActive) {
        if (now - keywordFlashStartMs < LED_KEYWORD_FLASH_MS) {
            bool all[MIC_COUNT] = {true, true, true, true};
            writeLeds(all, LED_PWM_BRIGHT);
            lastOutputDesc = "ALL (KEYWORD flash)";
            lastPatternDesc = "KEYWORD";
            return;
        }
        keywordFlashActive = false;
    }

    // Stale CONTINUOUS pulsing shouldn't leak from CANDIDATE/CRITICAL-EVENT
    // into a state that no longer justifies it.
    if (state != AwarenessState::CANDIDATE && state != AwarenessState::EVENT) {
        if (pulseMode == PulseMode::CONTINUOUS) pulseMode = PulseMode::NONE;
    }

    bool dirLeds[MIC_COUNT];
    ledsForDirection(direction, dirLeds);

    // Unknown direction on a fresh announcement: one brief simultaneous
    // flash -- never a false single-direction claim.
    if (direction == Direction::UNKNOWN && eventJustAnnounced) {
        unknownFlashActive = true;
        unknownFlashStartMs = now;
        pulseMode = PulseMode::NONE;
    }
    if (unknownFlashActive) {
        if (now - unknownFlashStartMs < LED_UNKNOWN_FLASH_MS) {
            bool all[MIC_COUNT] = {true, true, true, true};
            writeLeds(all, brightnessForConfidence(directionConfidence));
            lastOutputDesc = "ALL (UNKNOWN flash)";
            lastPatternDesc = "NONE";
            return;
        }
        unknownFlashActive = false;
    }

    int brightness = brightnessForConfidence(directionConfidence);

    switch (state) {
        case AwarenessState::BACKGROUND:
            criticalAcknowledged = false;
            allOutputsOff();
            return;

        case AwarenessState::NOTICE:
            if (noticeJustAnnounced) {
                pulseMode = PulseMode::FIXED_COUNT;
                pulsesRemaining = 1;  // a single brief flash, not a false alarm
                pulseOn = false;
                lastToggleMs = now;
                lastPatternDesc = "NOTICE";
            }
            break;

        case AwarenessState::CANDIDATE:
            // Keep flashing while still building confidence -- not confirmed yet.
            pulseMode = PulseMode::CONTINUOUS;
            lastPatternDesc = "CANDIDATE";
            break;

        case AwarenessState::EVENT:
            if (eventJustAnnounced) {
                // A fresh onset -- any previous acknowledgment applied to a
                // different (now-past) event and no longer applies.
                criticalAcknowledged = false;
                int count = pulseCountForPriority(priority);
                pulseMode = (count < 0) ? PulseMode::CONTINUOUS : PulseMode::FIXED_COUNT;
                pulsesRemaining = (count < 0) ? 0 : count;
                pulseOn = false;
                lastToggleMs = now;
                lastPatternDesc = priorityDescription(priority);
            } else if (pulseMode == PulseMode::NONE) {
                // Pattern already finished -- hold solid ON, except CRITICAL,
                // which keeps pulsing for the whole event (per spec: "rapid
                // repeated pulses", not a fixed count) -- unless the user
                // has acknowledged it (acknowledgeCriticalAlert()), in which
                // case it also just holds solid like any other priority.
                if (priority == Priority::PRIORITY_CRITICAL && !criticalAcknowledged) {
                    pulseMode = PulseMode::CONTINUOUS;
                } else {
                    writeLeds(dirLeds, brightness);
                    lastOutputDesc = directionDescription(dirLeds);
                    return;
                }
            }
            break;

        case AwarenessState::COOLDOWN:
            criticalAcknowledged = false;
            allOutputsOff();
            return;
    }

    // ---- advance the non-blocking pulse sequencer ----
    if (pulseMode != PulseMode::NONE) {
        if (now - lastToggleMs >= LED_PULSE_HALF_PERIOD_MS) {
            lastToggleMs = now;
            pulseOn = !pulseOn;
            if (!pulseOn && pulseMode == PulseMode::FIXED_COUNT) {
                pulsesRemaining--;
                if (pulsesRemaining <= 0) pulseMode = PulseMode::NONE;
            }
        }
        bool showLeds[MIC_COUNT];
        if (pulseOn) {
            memcpy(showLeds, dirLeds, sizeof(dirLeds));
        } else {
            for (int i = 0; i < MIC_COUNT; i++) showLeds[i] = false;
        }
        writeLeds(showLeds, brightness);
    } else {
        writeLeds(dirLeds, brightness);
    }
    lastOutputDesc = directionDescription(dirLeds);
}

bool handleTestCommand(char c) {
    bool pattern[MIC_COUNT];

    switch (c) {
        case 'F': ledsForDirection(Direction::FRONT, pattern); break;
        case 'R': ledsForDirection(Direction::RIGHT, pattern); break;
        case 'B': ledsForDirection(Direction::BACK, pattern); break;
        case 'L': ledsForDirection(Direction::LEFT, pattern); break;
        case 'A': ledsForDirection(Direction::OMNIDIRECTIONAL, pattern); break;
        case '1': ledsForDirection(Direction::FRONT, pattern); break;
        case '2': ledsForDirection(Direction::RIGHT, pattern); break;
        case '3': ledsForDirection(Direction::BACK, pattern); break;
        case '4': ledsForDirection(Direction::LEFT, pattern); break;
        case '5': ledsForDirection(Direction::FRONT_RIGHT, pattern); break;
        case '6': ledsForDirection(Direction::RIGHT_BACK, pattern); break;
        case '7': ledsForDirection(Direction::BACK_LEFT, pattern); break;
        case '8': ledsForDirection(Direction::LEFT_FRONT, pattern); break;
        case '9': ledsForDirection(Direction::OMNIDIRECTIONAL, pattern); break;
        case '0':
            manualMode = false;
            allOutputsOff();
            Serial.println("Manual mode OFF -- resuming automatic audio-driven output.");
            return true;
        default:
            return false;
    }

    manualMode = true;
    pulseMode = PulseMode::NONE;
    writeLeds(pattern, LED_PWM_BRIGHT);
    lastOutputDesc = directionDescription(pattern);
    lastPatternDesc = "MANUAL";

    Serial.print(lastOutputDesc);
    Serial.println(" LED(s) ON (manual test)");
    return true;
}

void triggerKeywordFlash() {
    keywordFlashActive = true;
    keywordFlashStartMs = millis();
}

void acknowledgeCriticalAlert() {
    criticalAcknowledged = true;
    // Stop any active pulsing right away rather than waiting for the next
    // updateOutput() call to notice via the EVENT/CRITICAL branch -- without
    // this the LEDs would keep pulsing until the current on/off half-cycle
    // ends (up to LED_PULSE_HALF_PERIOD_MS late).
    if (pulseMode == PulseMode::CONTINUOUS) {
        pulseMode = PulseMode::NONE;
    }
}

bool isManualModeActive() { return manualMode; }
const char* lastOutputDescription() { return lastOutputDesc; }
const char* lastPatternDescription() { return lastPatternDesc; }
