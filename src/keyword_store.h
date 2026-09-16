#pragma once
#include <stdint.h>
#include "mfcc_dsp.h"

// keyword_store module: flash-backed (NVS) storage for enrolled keyword/
// name templates, uploaded from the phone over BLE (Keyword Upload
// characteristic, ble_server.cpp) after a "say it 5 times" enrollment flow
// (KeywordEnrollScreen.kt).
//
// v2 (2026-09-15, after a second opinion on the whole matching approach):
// each keyword now stores UP TO KEYWORD_TEMPLATES_PER_WORD templates (one
// per enrollment recording, typically 3-5), not one medoid-selected
// template. Discarding 4 of 5 recordings' worth of real pronunciation
// variability down to a single "representative" template was flagged as
// the single highest-priority fix after live testing kept showing a
// single global threshold + one template per word struggling with
// near-duplicate keywords (two similarly-pronounced enrolled words
// competing for "closest match" with no way to express "this is
// ambiguous, not a confident match"). keyword_detector.cpp now matches a
// live utterance against every stored template of every keyword and uses
// both the best score AND the margin to the runner-up to decide, not one
// number.

// v3 (2026-09-16): shrunk from 60/5 to 40/3 after discovering the REAL
// ceiling isn't judgment-call "personal-device capacity" but the actual NVS
// partition size -- read directly off the chip, the "nvs" partition is only
// 20480 bytes (0x9000, size 0x5000; see platformio.ini, default partition
// table for this board). At 60/5, one keyword's fixed-size-padded blob was
// ~15.6KB and KEYWORD_MAX_COUNT=2 meant a worst case of ~31KB -- already
// over the ENTIRE partition before NVS's own per-entry/page overhead, with
// persist()'s putBytes() never checking its return value. That combination
// produced a completely silent failure: enrollment appeared to succeed
// (RAM-resident data matched fine within the same boot session) but never
// durably committed, so `nvs_open` for the "keywords" namespace came back
// NOT_FOUND on every subsequent boot, wiping every enrollment. At 40/3, one
// keyword's worst case (all templates at the max frame count) is ~6.3KB,
// so KEYWORD_MAX_COUNT=2's worst case is ~12.5KB -- comfortably under
// 20480 with real headroom for NVS overhead. persist() now also checks and
// logs putBytes()'s return value (keyword_store.cpp) so a future capacity
// regression fails loudly instead of silently.
static const int KEYWORD_MAX_TEMPLATE_FRAMES = 40; // ~960ms @ MFCC_FRAME_HOP, still generous for a single word/name
static const int KEYWORD_TEMPLATE_MAX_FLOATS = MFCC_COUNT * KEYWORD_MAX_TEMPLATE_FRAMES;
static const int KEYWORD_TEMPLATES_PER_WORD = 3; // matches ENROLLMENT_REPS in KeywordEnrollScreen.kt
#define KEYWORD_MAX_COUNT 2
#define KEYWORD_MAX_NAME_LEN 24

void keywordStoreInit();

// Call once per main loop frame -- keywordStoreAddTemplate() only updates
// the in-memory array and sets a dirty flag; this does the actual
// (blocking) NVS flash write, kept out of ble_server.cpp's
// KeywordUploadCallbacks::onWrite() (NimBLE host task, time-critical for
// the connection) and run from loop() instead.
void keywordStorePersistPending();

int keywordStoreCount(); // number of distinct enrolled keyword NAMES
const char* keywordStoreName(int idx);
int keywordStoreTemplateCount(int idx); // how many templates keyword idx actually has (<= KEYWORD_TEMPLATES_PER_WORD)
// Returns keyword idx's templateIdx-th MFCC sequence (keywordStoreFrameCount
// frames, MFCC_COUNT floats each, contiguous), or nullptr if either index
// is out of range.
const float* keywordStoreTemplate(int idx, int templateIdx);
int keywordStoreFrameCount(int idx, int templateIdx);

// Writes one of a keyword's up-to-KEYWORD_TEMPLATES_PER_WORD template
// slots (creating the keyword if this is its first template) and marks the
// result for persisting. Returns false if templateIndex is out of range,
// frameCount exceeds KEYWORD_MAX_TEMPLATE_FRAMES, or (for a brand new name)
// the store is already at KEYWORD_MAX_COUNT.
bool keywordStoreAddTemplate(const char* name, int templateIndex, const float *mfccFrames, int frameCount);

// Removes the keyword (and all its templates) with this exact name,
// shifting later slots down to fill the gap, and marks the result for
// persisting. Returns false if no keyword with that name exists.
bool keywordStoreRemove(const char* name);

void keywordStoreClearAll();
