package com.hapticbelt.app.data.notifications

import com.hapticbelt.app.data.Priority

// A haptic pattern derived from a phone notification. Pulse counts mirror the
// same LOW/MEDIUM/HIGH/CRITICAL escalation already used for real sound events
// (see PriorityEngine on the firmware side) so a deaf user learns one vocabulary
// of buzzes regardless of whether the trigger was a microphone or a phone
// notification. There are no real motors yet -- see BeltRepository.handleNotificationEvent
// for exactly what this currently drives (in-app state only) versus what still
// needs hardware (an actual buzz on the belt).
data class HapticPattern(
    val priority: Priority,
    val pulseCount: Int,
    val patternName: String
)

// One notification the listener service captured and classified. Kept even
// for notifications the bridge decides to ignore is NOT done here -- ignored
// notifications never become a CapturedNotification at all (see
// NotificationMapper.classify returning null). This log exists specifically
// so the capture+mapping logic is visible and checkable from inside the app
// without any belt hardware.
data class CapturedNotification(
    val id: Long,
    val timestampMillis: Long,
    val appLabel: String,
    val packageName: String,
    val category: String?,
    val title: String?,
    val pattern: HapticPattern
)
