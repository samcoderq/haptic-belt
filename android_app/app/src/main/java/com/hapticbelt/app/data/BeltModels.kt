package com.hapticbelt.app.data

// Mirrors the ESP32 firmware's own types (spatial_detector.h's Direction,
// temporal_reasoner.h's AwarenessState, priority_engine.h's Priority) --
// deliberately kept name-for-name identical so parsing real BLE data later
// is a direct mapping, not a redesign.

enum class Direction {
    FRONT, RIGHT, BACK, LEFT,
    FRONT_RIGHT, RIGHT_BACK, BACK_LEFT, LEFT_FRONT,
    OMNIDIRECTIONAL, UNKNOWN
}

enum class AwarenessState { BACKGROUND, NOTICE, CANDIDATE, EVENT, COOLDOWN }

enum class Priority { LOW, MEDIUM, HIGH, CRITICAL }

enum class Trend { APPROACHING, RECEDING, STABLE }

data class BeltState(
    val connected: Boolean = false,
    val direction: Direction = Direction.UNKNOWN,
    val directionConfidence: Float = 0f,
    val eventScore: Float = 0f,
    val eventConfidence: Float = 0f,
    val state: AwarenessState = AwarenessState.BACKGROUND,
    val priority: Priority = Priority.LOW,
    val sameDirectionRepeats: Int = 0,
    val trend: Trend = Trend.STABLE,
    val classificationLabel: String = "UNKNOWN",
    val lastEventAtMillis: Long? = null,
    // Set only by BleBeltRepository, from Belt State packet byte 12 (a
    // one-shot pulse on the firmware side, see ble_server.cpp) -- the name
    // is a best-effort local lookup (EnrolledKeywordsStore.nameForSlot()),
    // not something the belt itself sends; null name with a non-null time
    // means a match fired for a slot this phone has no local record of
    // (e.g. enrolled from a different phone, or local data was cleared).
    val lastKeywordMatchName: String? = null,
    val lastKeywordMatchAtMillis: Long? = null,
    // Persists for the actual duration of a live call -- unlike
    // classificationLabel/state above, which the belt's own ~10Hz real-time
    // state stream overwrites within ~100ms regardless of what caused the
    // last change. Set when NotificationMapper confirms an incoming call
    // notification, cleared when NotificationCaptureService sees that same
    // notification removed (answered/declined/ended) -- see
    // BeltRepository.handleNotificationEvent / handleCallEnded.
    val activeCallLabel: String? = null,
    val activeCallPackage: String? = null
)

// One entry in the Live Awareness / history feed.
data class BeltEvent(
    val id: Long,
    val timestampMillis: Long,
    val direction: Direction,
    val directionConfidence: Float,
    val priority: Priority,
    val eventConfidence: Float,
    val classificationLabel: String
)

enum class OperatingMode { HOME, OUTDOOR, WORKPLACE, SLEEP }

data class BeltSettings(
    val operatingMode: OperatingMode = OperatingMode.HOME,
    val vibrationIntensity: Float = 0.8f,      // 0..1, applies once real motors exist
    val sensitivity: Float = 0.5f,              // 0..1, maps to firmware threshold presets later
    val notificationBridgeEnabled: Boolean = true,
    val emergencyContactName: String = "",
    val emergencyContactPhone: String = "",
    val acknowledgeTimeoutSeconds: Int = 20
)
