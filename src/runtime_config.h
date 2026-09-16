#pragma once

// runtime_config module: the belt-firmware side of what the app's Settings
// screen (operating mode + sensitivity slider) is supposed to control.
// Before this, BLE_PROTOCOL.md's Settings characteristic could be received
// (ble_server.cpp decodes and stores it) but nothing in the detection
// pipeline actually changed in response -- the docs flagged this gap
// explicitly ("sensitivity presets aren't wired into config.h yet").
//
// Only the awareness state-machine thresholds (config.h's "Three-tier
// awareness thresholds" section) are made runtime-adjustable here -- those
// are what "sensitivity" and "operating mode" conceptually mean to a user on
// the Settings screen. Everything else (baseline alphas, event-score fusion
// weights, repeat-detection constants, ...) stays compile-time; there's no
// user-facing control mapped to those.

enum class OperatingMode { HOME, OUTDOOR, WORKPLACE, SLEEP };

extern float g_noticeEnterThreshold;
extern float g_noticeExitThreshold;
extern float g_candidateThreshold;
extern float g_instantThreshold;
extern float g_deactivateThreshold;

// Recomputes the 5 thresholds above from config.h's original defaults,
// scaled by a per-mode multiplier and the sensitivity slider (0..1, from
// BLE_PROTOCOL.md's Settings characteristic). Higher sensitivity -> lower
// thresholds -> catches quieter/farther sounds, matching the description
// already shown on the app's Settings screen. Cheap enough to call every
// frame rather than tracking whether settings actually changed.
void applyRuntimeSensitivity(OperatingMode mode, float sensitivity);
