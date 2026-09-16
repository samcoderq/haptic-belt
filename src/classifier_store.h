#pragma once
#include <stdint.h>

// classifier_store module: runtime-uploadable replacement for what used to
// be a compile-time classifier_model.h header. A trained sound class is now
// something the phone computes (from its own recorded samples) and sends
// over BLE (see ble_server.cpp's Class Upload characteristic) -- the belt
// just stores it, in flash (NVS, survives power-off/reboot), with no
// firmware recompile or reflash involved. This is what makes "record 5
// samples in the app" actually produce a working classifier on a real
// finished product instead of requiring a laptop in the loop.

#define CLASSIFIER_FEATURE_COUNT 16
#define CLASSIFIER_BLOCK_SIZE 1024
#define CLASSIFIER_MAX_CLASSES 8
#define CLASSIFIER_MAX_NAME_LEN 24  // including the null terminator

// Must match tools/goertzel.py's TARGET_FREQS and the Android app's
// GoertzelFeatures.kt exactly -- all three sides compute the same feature
// vector shape so a centroid trained on the phone means the same thing when
// compared on-device.
extern const float CLASSIFIER_TARGET_FREQS[CLASSIFIER_FEATURE_COUNT];

// Loads whatever classes were previously uploaded and persisted (empty on
// first-ever boot, or after classifierStoreClearAll()). Call once from setup().
void classifierStoreInit();

// Call once per main loop frame -- same reasoning and pattern as
// keywordStorePersistPending() in keyword_store.h: classifierStoreAddClass()
// only updates the in-memory array and sets a dirty flag now; this does the
// actual (blocking) NVS flash write, kept out of ble_server.cpp's
// ClassUploadCallbacks::onWrite() (NimBLE host task, time-critical for the
// connection) and run from loop() instead.
void classifierStorePersistPending();

int classifierStoreClassCount();
const char* classifierStoreClassName(int idx);
const float* classifierStoreCentroid(int idx); // CLASSIFIER_FEATURE_COUNT floats, or nullptr if idx out of range

// Adds a new class, or replaces the centroid of an existing one with the
// same name, and persists immediately. Returns false only if at capacity
// (CLASSIFIER_MAX_CLASSES) and name is new -- the phone side should surface
// that rather than silently dropping the upload.
bool classifierStoreAddClass(const char* name, const float centroid[CLASSIFIER_FEATURE_COUNT]);

void classifierStoreClearAll();
