#pragma once
#include <stdint.h>
#include "spatial_detector.h"
#include "temporal_reasoner.h"
#include "priority_engine.h"

// ble_server module: implements the GATT service described in
// android_app/BLE_PROTOCOL.md (v1) -- the firmware side that document's own
// Section 7 checklist asked for, previously entirely unimplemented (no BLE
// code existed anywhere in this firmware before this file). Never tested
// against the real Android app or real hardware; verified only by this
// module compiling and by hand-checking every offset/ordinal/scale factor
// against the protocol doc, same honesty standard the doc itself holds to.
//
// Deliberately isolated from every other module: main.cpp is the only
// caller, and it already computes everything this needs each frame -- this
// file just encodes it onto the wire, exactly like classifier_interface.h
// is the seam for a classifier, this is the seam for connectivity.

struct BleSettings {
    uint8_t operatingMode = 0;       // 0=HOME,1=OUTDOOR,2=WORKPLACE,3=SLEEP
    float vibrationIntensity = 0.8f; // 0..1
    float sensitivity = 0.5f;        // 0..1
};

void bleServerInit();
bool bleServerIsConnected();

// Call once per main loop frame. Builds and notifies the 13-byte Belt State
// packet (BLE_PROTOCOL.md Section 3). isOnset should be
// (temporal.noticeAnnounce || temporal.eventAnnounce) -- the same condition
// main.cpp already uses -- and is what advances eventSeq.
//
// keywordMatchSlot: 0 = no keyword match this frame, 1..KEYWORD_MAX_COUNT =
// (keyword_store slot index + 1) of a match THIS frame only -- a one-shot
// pulse, not a persisted state, matching how keyword_detector.cpp itself
// only reports matched=true on the exact frame it fires. Was BLE_PROTOCOL.md
// Section 3's reserved byte 12 (always 0) before this.
void bleServerNotifyState(Direction direction, float directionConfidence,
                           float eventScore, float eventConfidence,
                           AwarenessState state, Priority priority,
                           int sameDirectionRepeats, bool isApproaching, bool isReceding,
                           const char* classificationLabel, bool isOnset,
                           uint8_t keywordMatchSlot);

// True exactly once -- the first call after a matching-eventSeq Acknowledge
// write has arrived -- so main.cpp can react (e.g. clear a CRITICAL pattern)
// without this module knowing anything about awareness_output.
bool bleServerConsumeAcknowledge(uint16_t &outAckedEventSeq);

// Latest settings received from the phone over the Settings characteristic
// (BLE_PROTOCOL.md Section 5), or the defaults above if nothing has been
// written yet this session.
BleSettings bleServerGetSettings();
