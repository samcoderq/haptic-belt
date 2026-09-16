#include "classifier_store.h"
#include <Preferences.h>
#include <string.h>

const float CLASSIFIER_TARGET_FREQS[CLASSIFIER_FEATURE_COUNT] = {
    200, 300, 400, 550, 700, 900, 1100, 1400,
    1700, 2100, 2600, 3200, 3900, 4700, 5700, 7000
};

struct StoredClass {
    char name[CLASSIFIER_MAX_NAME_LEN];
    float centroid[CLASSIFIER_FEATURE_COUNT];
};

static StoredClass classes[CLASSIFIER_MAX_CLASSES];
static int classCount = 0;
static bool dirty = false;

static Preferences prefs;
static const char* NVS_NAMESPACE = "classifier";
static const char* NVS_KEY = "classes";

// Blob layout: [uint8 count][StoredClass * count] -- simplest thing that
// works; total size (<= 1 + 8*88 = 705 bytes) is trivial for NVS.
static void persist() {
    prefs.begin(NVS_NAMESPACE, false);
    uint8_t buf[1 + CLASSIFIER_MAX_CLASSES * sizeof(StoredClass)];
    buf[0] = (uint8_t)classCount;
    memcpy(buf + 1, classes, sizeof(StoredClass) * classCount);
    prefs.putBytes(NVS_KEY, buf, 1 + sizeof(StoredClass) * classCount);
    prefs.end();
}

void classifierStoreInit() {
    classCount = 0;
    prefs.begin(NVS_NAMESPACE, true); // read-only
    size_t len = prefs.getBytesLength(NVS_KEY);
    if (len >= 1) {
        uint8_t buf[1 + CLASSIFIER_MAX_CLASSES * sizeof(StoredClass)];
        size_t toRead = len < sizeof(buf) ? len : sizeof(buf);
        prefs.getBytes(NVS_KEY, buf, toRead);
        uint8_t storedCount = buf[0];
        if (storedCount > CLASSIFIER_MAX_CLASSES) storedCount = CLASSIFIER_MAX_CLASSES;
        size_t expected = 1 + sizeof(StoredClass) * storedCount;
        if (toRead >= expected) {
            memcpy(classes, buf + 1, sizeof(StoredClass) * storedCount);
            classCount = storedCount;
        }
    }
    prefs.end();
}

int classifierStoreClassCount() { return classCount; }

const char* classifierStoreClassName(int idx) {
    if (idx < 0 || idx >= classCount) return nullptr;
    return classes[idx].name;
}

const float* classifierStoreCentroid(int idx) {
    if (idx < 0 || idx >= classCount) return nullptr;
    return classes[idx].centroid;
}

bool classifierStoreAddClass(const char* name, const float centroid[CLASSIFIER_FEATURE_COUNT]) {
    int existing = -1;
    for (int i = 0; i < classCount; i++) {
        if (strncmp(classes[i].name, name, CLASSIFIER_MAX_NAME_LEN) == 0) {
            existing = i;
            break;
        }
    }
    int idx;
    if (existing >= 0) {
        idx = existing;
    } else {
        if (classCount >= CLASSIFIER_MAX_CLASSES) return false;
        idx = classCount;
        classCount++;
    }
    strncpy(classes[idx].name, name, CLASSIFIER_MAX_NAME_LEN - 1);
    classes[idx].name[CLASSIFIER_MAX_NAME_LEN - 1] = '\0';
    memcpy(classes[idx].centroid, centroid, sizeof(float) * CLASSIFIER_FEATURE_COUNT);
    dirty = true; // actual flash write deferred to classifierStorePersistPending() -- see classifier_store.h
    return true;
}

void classifierStorePersistPending() {
    if (!dirty) return;
    persist();
    dirty = false;
}

void classifierStoreClearAll() {
    classCount = 0;
    persist();
}
