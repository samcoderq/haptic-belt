#pragma once
#include "spatial_detector.h"
#include "temporal_reasoner.h"
#include "priority_engine.h"

// awareness_output module: the ONLY place that knows about physical output
// hardware. Today that's 4 LEDs; later it becomes 4 vibration motors behind
// the exact same function signatures -- event detection, spatial detection,
// and priority never need to change when that swap happens.
//
//   event -> direction -> awareness_output -> LED (today)
//   event -> direction -> awareness_output -> haptic motor (later)

void awarenessOutputInit();
void allOutputsOff();

// Call once per loop() iteration with the latest pipeline results. Runs a
// non-blocking pulse/flash state machine internally (millis()-based, no
// delay() calls) so it never affects audio timing.
void updateOutput(Direction direction, float directionConfidence, AwarenessState state,
                   Priority priority, bool eventJustAnnounced, bool noticeJustAnnounced);

// Serial test commands that bypass audio entirely:
//   F/R/B/L/A/0  -> raw single-LED wiring test (sticky until next command)
//   1-9/0        -> manual direction test (FRONT..LEFT, merged pairs, ALL, OFF)
// '0' also exits manual mode and resumes automatic audio-driven output.
// Returns true if the character was a recognized test command.
bool handleTestCommand(char c);

bool isManualModeActive();

const char* lastOutputDescription();   // e.g. "RIGHT", "FRONT+RIGHT", "ALL", "OFF"
const char* lastPatternDescription();  // e.g. "HIGH", "MANUAL", "NONE"
