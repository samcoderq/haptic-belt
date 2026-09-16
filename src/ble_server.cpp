#include "ble_server.h"
#include "classifier_store.h"
#include "keyword_store.h"
#include <NimBLEDevice.h>
#include <math.h>
#include <string.h>

// UUIDs and packet layout: android_app/BLE_PROTOCOL.md Sections 2-6.
static const char* SERVICE_UUID          = "81b24d0d-94dd-4ccf-a8b7-b5e24cd00331";
static const char* STATE_CHAR_UUID       = "aa596361-ca3a-4c37-8d74-f8fa8f8b487d";
static const char* SETTINGS_CHAR_UUID    = "b8cf1a57-edf2-4912-9c33-c6ce2eba0046";
static const char* ACK_CHAR_UUID         = "c776acfe-db36-4956-922d-5f13ddaf8584";
// Class Upload (Write) -- v1.1 protocol addition, not yet in BLE_PROTOCOL.md
// when this was written (see the doc for the pending write-up). Lets the
// phone push a trained sound class (a name + a 16-float centroid, computed
// from its own recorded samples) straight into classifier_store's NVS-
// backed storage -- no firmware reflash needed to add/update a class.
// Wire format: [nameLen:1][name:nameLen ASCII][centroid:64 bytes, 16 LE float32].
// Needs a negotiated MTU > 23 (the other three characteristics deliberately
// don't) since 1+23+64=88 bytes doesn't fit the default 20-byte payload.
static const char* CLASS_UPLOAD_CHAR_UUID = "551f1f19-451e-419a-b181-817bb8997fcc";
// Keyword Upload (Write) -- v1.2 addition, same purpose as Class Upload but
// for a spoken keyword/name enrolled via KeywordEnrollScreen.kt's "say it 5
// times" flow. A keyword template is a SEQUENCE of feature blocks (captures
// a word's temporal shape), not one static vector.
//
// v1.3: chunked, one write per segment, NOT one write of the whole template
// in a single packet. A single-write design was this project's first
// real-hardware BLE test and consistently killed the connection outright
// (status=133) despite a correctly negotiated 512 MTU -- a link/controller-
// level issue with a write that large, not an application bug on either
// side. A small write per segment (well within what was already confirmed
// to handle reliably) sidesteps it entirely, at the cost of one round-trip
// per segment instead of one total.
//
// v1.4: each segment is now one MFCC frame (MFCC_COUNT floats), NOT one of
// a fixed KEYWORD_SEGMENT_COUNT block of Goertzel bins -- keyword_detector.cpp
// was rewritten from Goertzel-bins + fixed-window Euclidean distance to
// MFCC + DTW (see its own header comment for why), and a template is now a
// VARIABLE-length sequence of frames (however long the enrolled word took
// to say, up to KEYWORD_MAX_TEMPLATE_FRAMES), not a fixed 7 segments.
//
// v1.6: gained a leading templateIndex byte -- each of a word's
// KEYWORD_TEMPLATES_PER_WORD enrollment recordings is uploaded (and
// matched against, live) as its own separate template now, instead of the
// phone picking one "representative" recording (originally an averaged
// vector, later a medoid pick) before ever uploading. That was flagged as
// the single highest-priority fix after live testing kept showing
// one-template-per-word + a single global threshold struggling with
// near-duplicate keywords, per keyword_store.h's own history comment.
// Wire format per write: [templateIndex:1][segmentIndex:1][totalSegments:1]
// [nameLen:1][name:nameLen ASCII][segment: MFCC_COUNT floats, LE float32].
// Segments must arrive in order (0..totalSegments-1) for the same
// (name, templateIndex) pair; the belt commits to
// keywordStoreAddTemplate() only once the final segment of a given
// template arrives, and silently drops the in-progress upload if segment 0
// doesn't match the expected start (e.g. a previous enrollment was
// abandoned mid-upload) or if totalSegments exceeds
// KEYWORD_MAX_TEMPLATE_FRAMES.
static const char* KEYWORD_UPLOAD_CHAR_UUID = "88e19205-b3c7-4cfd-8ec2-691dadcdb0ac";
// Keyword Delete (Write) -- v1.5 addition. Removes one enrolled keyword by
// name. Added because "Remove from list" in KeywordEnrollScreen.kt was
// local-only (EnrolledKeywordsStore, the app's own record) with no way to
// actually reach the belt -- a real gap a real user hit: removing a
// keyword from the app's list had every reason to look like it was
// actually gone, not just hidden from one screen. Wire format:
// [nameLen:1][name:nameLen ASCII]. Silently a no-op if no keyword with that
// name is currently stored.
static const char* KEYWORD_DELETE_CHAR_UUID = "68d90930-8ab9-43b3-b5fb-97dc0c9657e3";

static const uint8_t PROTOCOL_VERSION = 1;

static NimBLEServer* server = nullptr;
static NimBLECharacteristic* stateChar = nullptr;
static NimBLECharacteristic* settingsChar = nullptr;
static NimBLECharacteristic* ackChar = nullptr;
static NimBLECharacteristic* classUploadChar = nullptr;
static NimBLECharacteristic* keywordUploadChar = nullptr;
static NimBLECharacteristic* keywordDeleteChar = nullptr;

static volatile bool deviceConnected = false;
static volatile uint16_t eventSeqCounter = 0;
static volatile uint16_t lastSentEventSeq = 0;

// Set by the Ack characteristic's write callback (runs on the NimBLE host
// task, not the Arduino loop task), consumed by bleServerConsumeAcknowledge()
// -- volatile since it crosses that task boundary.
static volatile bool ackPending = false;
static volatile uint16_t ackedEventSeq = 0;

static volatile uint8_t settingsOperatingMode = 0;
static volatile float settingsVibrationIntensity = 0.8f;
static volatile float settingsSensitivity = 0.5f;

static uint8_t floatToByte(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return (uint8_t)(v * 255.0f + 0.5f);
}

// Mirrors BleProtocol.kt's classificationLabelFromCode table (BLE_PROTOCOL.md
// Section 4.1) in reverse -- label string to wire code. Anything unrecognized
// encodes as UNKNOWN (0), matching the protocol's own forward-compatibility rule.
static uint8_t classificationCodeFromLabel(const char* label) {
    static const char* LABELS[] = {
        "UNKNOWN", "SPEECH", "ALARM", "SIREN", "HORN",
        "KNOCK_OR_DOORBELL", "PHONE_RINGTONE", "DOG_BARK", "GLASS_BREAK"
    };
    for (uint8_t i = 0; i < sizeof(LABELS) / sizeof(LABELS[0]); i++) {
        if (strcmp(label, LABELS[i]) == 0) return i;
    }
    return 0;
}

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* srv) override {
        deviceConnected = true;
    }
    void onDisconnect(NimBLEServer* srv) override {
        deviceConnected = false;
        // NimBLE does not auto-resume advertising after a disconnect.
        NimBLEDevice::startAdvertising();
    }
};

class AckCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        std::string value = c->getValue();
        if (value.size() < 3) return; // malformed write, ignore
        uint8_t command = (uint8_t)value[0];
        uint16_t seq = (uint8_t)value[1] | ((uint8_t)value[2] << 8);
        if (command == 1) { // ACKNOWLEDGE_EVENT
            // BLE_PROTOCOL.md Section 6: only accept an ack that matches the
            // most recently notified eventSeq -- a stale ack (e.g. sent
            // right as a NEW event starts, a race the phone can't fully
            // prevent) must not clear the wrong event. Previously this
            // check was only documented, not actually implemented.
            if (seq == eventSeqCounter) {
                ackedEventSeq = seq;
                ackPending = true;
            }
        }
    }
};

class SettingsCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        std::string value = c->getValue();
        if (value.size() < 4) return; // malformed write, ignore
        // value[0] is protocolVersion -- not rejected on mismatch, per the
        // same forward-compatible spirit as the classification code table.
        settingsOperatingMode = (uint8_t)value[1];
        settingsVibrationIntensity = (uint8_t)value[2] / 255.0f;
        settingsSensitivity = (uint8_t)value[3] / 255.0f;
    }
};

class ClassUploadCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        std::string value = c->getValue();
        if (value.size() < 1) return;
        uint8_t nameLen = (uint8_t)value[0];
        size_t expected = 1 + nameLen + CLASSIFIER_FEATURE_COUNT * sizeof(float);
        if (nameLen == 0 || nameLen >= CLASSIFIER_MAX_NAME_LEN || value.size() < expected) {
            return; // malformed/truncated write (e.g. MTU too small), ignore
        }
        char name[CLASSIFIER_MAX_NAME_LEN];
        memcpy(name, value.data() + 1, nameLen);
        name[nameLen] = '\0';

        float centroid[CLASSIFIER_FEATURE_COUNT];
        memcpy(centroid, value.data() + 1 + nameLen, CLASSIFIER_FEATURE_COUNT * sizeof(float));
        // Wire format is little-endian float32 (BLE_PROTOCOL.md's convention
        // for every multi-byte field); Xtensa is little-endian too, so this
        // raw memcpy is correct as-is -- no byte-swapping needed.

        classifierStoreAddClass(name, centroid);
    }
};

class KeywordUploadCallbacks : public NimBLECharacteristicCallbacks {
    // Accumulates across multiple writes (one per segment == one MFCC
    // frame) -- see the KEYWORD_UPLOAD_CHAR_UUID comment above for why this
    // is chunked at all. Static (not per-connection): this project has
    // never supported more than one phone talking to a belt at once, so a
    // single in-progress upload's worth of state is sufficient.
    //
    // v1.6: wire format gained a leading templateIndex byte -- each of a
    // word's KEYWORD_TEMPLATES_PER_WORD enrollment recordings is now
    // uploaded and stored as its own template (keywordStoreAddTemplate,
    // one BLE upload per recording, same chunked-per-frame mechanism
    // repeated once per recording) instead of the app picking one
    // "representative" recording before uploading -- see keyword_store.h's
    // history comment for why.
    char pendingName[KEYWORD_MAX_NAME_LEN] = {0};
    float pendingTemplate[KEYWORD_TEMPLATE_MAX_FLOATS] = {0};
    uint8_t pendingTemplateIndex = 0;
    uint8_t pendingTotalSegments = 0;
    int nextExpectedSegment = 0;

    void onWrite(NimBLECharacteristic* c) override {
        std::string value = c->getValue();
        if (value.size() < 4) return;
        uint8_t templateIndex = (uint8_t)value[0];
        uint8_t segmentIndex = (uint8_t)value[1];
        uint8_t totalSegments = (uint8_t)value[2];
        uint8_t nameLen = (uint8_t)value[3];
        size_t expected = 4 + nameLen + MFCC_COUNT * sizeof(float);
        if (templateIndex >= KEYWORD_TEMPLATES_PER_WORD ||
            totalSegments == 0 || totalSegments > KEYWORD_MAX_TEMPLATE_FRAMES ||
            nameLen == 0 || nameLen >= KEYWORD_MAX_NAME_LEN || value.size() < expected) {
            return; // malformed/truncated write (e.g. MTU too small), ignore
        }

        if (segmentIndex == 0) {
            memcpy(pendingName, value.data() + 4, nameLen);
            pendingName[nameLen] = '\0';
            pendingTemplateIndex = templateIndex;
            pendingTotalSegments = totalSegments;
            nextExpectedSegment = 0;
        } else if (segmentIndex != nextExpectedSegment ||
                   totalSegments != pendingTotalSegments ||
                   templateIndex != pendingTemplateIndex ||
                   strncmp(pendingName, value.data() + 4, nameLen) != 0 ||
                   pendingName[nameLen] != '\0') {
            // Out of order, a different totalSegments/templateIndex
            // mid-upload, or doesn't match the name segment 0 started --
            // drop the in-progress upload rather than risk stitching
            // together segments from two different enrollments.
            //
            // Diagnostic (2026-09-16): previously silent -- a GATT write can
            // report GATT_SUCCESS to the phone (the bytes arrived fine) even
            // when this app-level check rejects it, which would look
            // identical to "enrollment never reached the belt" from the
            // phone's side. Temporary debug print for the ongoing
            // enrollment-loss investigation.
            Serial.print("Keyword upload segment REJECTED: name='"); Serial.print(pendingName);
            Serial.print("' expectedSeg="); Serial.print(nextExpectedSegment);
            Serial.print(" gotSeg="); Serial.println(segmentIndex);
            nextExpectedSegment = 0;
            return;
        }

        memcpy(pendingTemplate + (size_t)segmentIndex * MFCC_COUNT,
               value.data() + 4 + nameLen, MFCC_COUNT * sizeof(float));
        nextExpectedSegment = segmentIndex + 1;

        if (segmentIndex == totalSegments - 1) {
            bool stored = keywordStoreAddTemplate(pendingName, pendingTemplateIndex, pendingTemplate, totalSegments);
            Serial.print("Keyword upload complete: name='"); Serial.print(pendingName);
            Serial.print("' templateIndex="); Serial.print(pendingTemplateIndex);
            Serial.print(" frames="); Serial.print(totalSegments);
            Serial.print(" stored="); Serial.println(stored ? "yes" : "NO (store full or bad params)");
            nextExpectedSegment = 0;
        }
    }
};

class KeywordDeleteCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        std::string value = c->getValue();
        if (value.size() < 1) return;
        uint8_t nameLen = (uint8_t)value[0];
        if (nameLen == 0 || nameLen >= KEYWORD_MAX_NAME_LEN || value.size() < (size_t)(1 + nameLen)) {
            return; // malformed/truncated write, ignore
        }
        char name[KEYWORD_MAX_NAME_LEN];
        memcpy(name, value.data() + 1, nameLen);
        name[nameLen] = '\0';
        bool removed = keywordStoreRemove(name);
        // Diagnostic (2026-09-16): so an unexpected delete (from anywhere,
        // not just the app's "Remove from list" button) is visible instead
        // of just silently emptying the store -- see KeywordUploadCallbacks
        // comment above for the same rationale.
        Serial.print("Keyword delete requested: name='"); Serial.print(name);
        Serial.print("' removed="); Serial.println(removed ? "yes" : "no (not found)");
    }
};

void bleServerInit() {
    NimBLEDevice::init("Haptic Belt");
    // Class Upload needs > the default 23-byte ATT MTU; Keyword Upload needs
    // even more (a template is a sequence of feature blocks, not one
    // vector) -- 512 covers both with room to spare, under BLE's 517-byte
    // hard MTU ceiling. The other three characteristics were deliberately
    // designed to need none of this.
    NimBLEDevice::setMTU(512);

    server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    NimBLEService* service = server->createService(SERVICE_UUID);

    stateChar = service->createCharacteristic(
        STATE_CHAR_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );
    uint8_t zeros[13] = {0};
    stateChar->setValue(zeros, sizeof(zeros));

    settingsChar = service->createCharacteristic(
        SETTINGS_CHAR_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
    );
    uint8_t defaultSettings[4] = {
        PROTOCOL_VERSION, 0,
        floatToByte(settingsVibrationIntensity),
        floatToByte(settingsSensitivity)
    };
    settingsChar->setValue(defaultSettings, sizeof(defaultSettings));
    settingsChar->setCallbacks(new SettingsCallbacks());

    ackChar = service->createCharacteristic(
        ACK_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE
    );
    ackChar->setCallbacks(new AckCallbacks());

    classUploadChar = service->createCharacteristic(
        CLASS_UPLOAD_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE
    );
    classUploadChar->setCallbacks(new ClassUploadCallbacks());

    keywordUploadChar = service->createCharacteristic(
        KEYWORD_UPLOAD_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE
    );
    keywordUploadChar->setCallbacks(new KeywordUploadCallbacks());

    keywordDeleteChar = service->createCharacteristic(
        KEYWORD_DELETE_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE
    );
    keywordDeleteChar->setCallbacks(new KeywordDeleteCallbacks());

    // Return values were never checked before -- a silent failure here would
    // look identical to a working belt right up until a phone tries (and
    // permanently fails) to find it, which is exactly what happened the
    // first time this was tested against real hardware.
    bool serviceStarted = service->start();
    Serial.print("BLE: service->start() "); Serial.println(serviceStarted ? "OK" : "FAILED");

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    bool advertisingStarted = advertising->start();
    Serial.print("BLE: advertising->start() "); Serial.println(advertisingStarted ? "OK" : "FAILED");
}

bool bleServerIsConnected() {
    return deviceConnected;
}

void bleServerNotifyState(Direction direction, float directionConfidence,
                           float eventScore, float eventConfidence,
                           AwarenessState state, Priority priority,
                           int sameDirectionRepeats, bool isApproaching, bool isReceding,
                           const char* classificationLabel, bool isOnset,
                           uint8_t keywordMatchSlot) {
    if (isOnset) {
        eventSeqCounter++; // wraps 65535 -> 0 naturally via uint16_t overflow
    }
    lastSentEventSeq = eventSeqCounter;

    // BLE_PROTOCOL.md Section 3's derivation rule.
    uint8_t trend = isApproaching ? 0 : (isReceding ? 1 : 2);

    uint8_t repeats = sameDirectionRepeats > 255 ? 255 : (uint8_t)sameDirectionRepeats;

    uint8_t packet[13];
    packet[0] = PROTOCOL_VERSION;
    packet[1] = (uint8_t)direction;
    packet[2] = floatToByte(directionConfidence);
    packet[3] = floatToByte(eventScore);
    packet[4] = floatToByte(eventConfidence);
    packet[5] = (uint8_t)state;
    packet[6] = (uint8_t)priority;
    packet[7] = repeats;
    packet[8] = trend;
    packet[9] = classificationCodeFromLabel(classificationLabel);
    packet[10] = (uint8_t)(eventSeqCounter & 0xFF);
    packet[11] = (uint8_t)((eventSeqCounter >> 8) & 0xFF);
    packet[12] = keywordMatchSlot; // was reserved(0); now a one-shot keyword-match pulse

    stateChar->setValue(packet, sizeof(packet));
    if (deviceConnected) {
        stateChar->notify();
    }
}

bool bleServerConsumeAcknowledge(uint16_t &outAckedEventSeq) {
    if (!ackPending) return false;
    ackPending = false;
    outAckedEventSeq = ackedEventSeq;
    return true;
}

BleSettings bleServerGetSettings() {
    BleSettings s;
    s.operatingMode = settingsOperatingMode;
    s.vibrationIntensity = settingsVibrationIntensity;
    s.sensitivity = settingsSensitivity;
    return s;
}
