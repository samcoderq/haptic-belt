#include "keyword_store.h"
#include <Preferences.h>
#include <string.h>
#include <Arduino.h>

struct StoredTemplate {
    uint8_t frameCount; // valid: [0, frameCount) * MFCC_COUNT floats in mfcc[]
    float mfcc[KEYWORD_TEMPLATE_MAX_FLOATS];
};

struct StoredKeyword {
    char name[KEYWORD_MAX_NAME_LEN];
    uint8_t templateCount; // how many of templates[] are actually filled
    StoredTemplate templates[KEYWORD_TEMPLATES_PER_WORD];
};

static StoredKeyword keywords[KEYWORD_MAX_COUNT];
static int keywordCount = 0;
static bool dirty = false;

static Preferences prefs;
static const char* NVS_NAMESPACE = "keywords";
static const char* NVS_KEY_COUNT = "count";
static const char* NVS_KEY_DATA  = "keywords";

// Two separate keys (count, data), and writing/reading the already-static
// `keywords` array directly rather than assembling a combined blob in a
// scratch buffer -- see this file's git history: an earlier version that
// assembled a combined [count][data] blob in a stack-local buffer
// overflowed the loop task's stack the moment per-keyword storage grew
// past a few hundred bytes, the same class of bug that hit two other files
// this project. That risk only grows with multi-template storage, so this
// pattern stays.
// v3 (2026-09-16): now checks putBytes()'s return value. A previous
// version didn't, which let an oversized write (keyword_store.h's history
// comment has the full story -- the NVS partition is only 20480 bytes)
// fail completely silently: the app reported "Sent", live matching worked
// within that boot session (RAM-resident), and every enrollment vanished
// on the next reboot with no diagnostic anywhere. This can't un-happen a
// write that's still too big, but it makes sure it's never silent again.
static void persist() {
    prefs.begin(NVS_NAMESPACE, false);
    uint8_t count = (uint8_t)keywordCount;
    size_t countWritten = prefs.putBytes(NVS_KEY_COUNT, &count, 1);
    size_t dataExpected = sizeof(StoredKeyword) * keywordCount;
    size_t dataWritten = prefs.putBytes(NVS_KEY_DATA, keywords, dataExpected);
    prefs.end();
    if (countWritten != 1 || dataWritten != dataExpected) {
        Serial.print("Keyword store PERSIST FAILED: countWritten="); Serial.print(countWritten);
        Serial.print(" dataWritten="); Serial.print(dataWritten);
        Serial.print("/"); Serial.print(dataExpected);
        Serial.println(" -- enrollment will NOT survive a reboot. Store likely exceeds NVS partition capacity.");
    }
}

void keywordStoreInit() {
    keywordCount = 0;
    prefs.begin(NVS_NAMESPACE, true); // read-only
    uint8_t storedCount = 0;
    if (prefs.getBytesLength(NVS_KEY_COUNT) >= 1) {
        prefs.getBytes(NVS_KEY_COUNT, &storedCount, 1);
    }
    if (storedCount > KEYWORD_MAX_COUNT) storedCount = KEYWORD_MAX_COUNT;
    size_t expected = sizeof(StoredKeyword) * storedCount;
    if (storedCount > 0 && prefs.getBytesLength(NVS_KEY_DATA) >= expected) {
        prefs.getBytes(NVS_KEY_DATA, keywords, expected);
        keywordCount = storedCount;
    }
    prefs.end();
}

int keywordStoreCount() { return keywordCount; }

const char* keywordStoreName(int idx) {
    if (idx < 0 || idx >= keywordCount) return nullptr;
    return keywords[idx].name;
}

int keywordStoreTemplateCount(int idx) {
    if (idx < 0 || idx >= keywordCount) return 0;
    return keywords[idx].templateCount;
}

const float* keywordStoreTemplate(int idx, int templateIdx) {
    if (idx < 0 || idx >= keywordCount) return nullptr;
    if (templateIdx < 0 || templateIdx >= keywords[idx].templateCount) return nullptr;
    return keywords[idx].templates[templateIdx].mfcc;
}

int keywordStoreFrameCount(int idx, int templateIdx) {
    if (idx < 0 || idx >= keywordCount) return 0;
    if (templateIdx < 0 || templateIdx >= keywords[idx].templateCount) return 0;
    return keywords[idx].templates[templateIdx].frameCount;
}

bool keywordStoreAddTemplate(const char* name, int templateIndex, const float *mfccFrames, int frameCount) {
    if (templateIndex < 0 || templateIndex >= KEYWORD_TEMPLATES_PER_WORD) return false;
    if (frameCount <= 0 || frameCount > KEYWORD_MAX_TEMPLATE_FRAMES) return false;

    int idx = -1;
    for (int i = 0; i < keywordCount; i++) {
        if (strncmp(keywords[i].name, name, KEYWORD_MAX_NAME_LEN) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        if (keywordCount >= KEYWORD_MAX_COUNT) return false;
        idx = keywordCount;
        keywordCount++;
        strncpy(keywords[idx].name, name, KEYWORD_MAX_NAME_LEN - 1);
        keywords[idx].name[KEYWORD_MAX_NAME_LEN - 1] = '\0';
        keywords[idx].templateCount = 0;
    }

    keywords[idx].templates[templateIndex].frameCount = (uint8_t)frameCount;
    memcpy(keywords[idx].templates[templateIndex].mfcc, mfccFrames, sizeof(float) * MFCC_COUNT * frameCount);
    if (templateIndex + 1 > keywords[idx].templateCount) {
        keywords[idx].templateCount = (uint8_t)(templateIndex + 1);
    }
    dirty = true; // actual flash write deferred to keywordStorePersistPending() -- see keyword_store.h
    return true;
}

bool keywordStoreRemove(const char* name) {
    int idx = -1;
    for (int i = 0; i < keywordCount; i++) {
        if (strncmp(keywords[i].name, name, KEYWORD_MAX_NAME_LEN) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return false;

    for (int i = idx; i < keywordCount - 1; i++) {
        keywords[i] = keywords[i + 1];
    }
    keywordCount--;
    dirty = true; // actual flash write deferred to keywordStorePersistPending() -- see keyword_store.h
    return true;
}

void keywordStorePersistPending() {
    if (!dirty) return;
    persist();
    dirty = false;
}

void keywordStoreClearAll() {
    keywordCount = 0;
    persist();
}
