package com.hapticbelt.app.data.ble

import com.hapticbelt.app.data.AwarenessState
import com.hapticbelt.app.data.Direction
import com.hapticbelt.app.data.OperatingMode
import com.hapticbelt.app.data.Priority
import com.hapticbelt.app.data.Trend
import java.util.UUID
import kotlin.math.roundToInt

// Wire format for the custom "Haptic Belt" GATT service. See
// ../../../../../../../../BLE_PROTOCOL.md (android_app/BLE_PROTOCOL.md) for the
// full spec and the reasoning behind every choice here -- this file must stay
// in lockstep with that document. Verified against real firmware: the State
// packet decode below has been exercised against live Belt State
// notifications from a connected belt.
object BleProtocol {
    const val PROTOCOL_VERSION: Int = 1

    val SERVICE_UUID: UUID = UUID.fromString("81b24d0d-94dd-4ccf-a8b7-b5e24cd00331")
    // Must match NimBLEDevice::init("Haptic Belt") in src/ble_server.cpp exactly --
    // used as a scan fallback alongside the service-UUID filter (some BLE stacks,
    // notably Samsung's, have been unreliable matching ScanFilter.setServiceUuid()
    // against a custom 128-bit UUID even when the advertisement data is correct).
    const val DEVICE_NAME: String = "Haptic Belt"
    val STATE_CHARACTERISTIC_UUID: UUID = UUID.fromString("aa596361-ca3a-4c37-8d74-f8fa8f8b487d")
    val SETTINGS_CHARACTERISTIC_UUID: UUID = UUID.fromString("b8cf1a57-edf2-4912-9c33-c6ce2eba0046")
    val ACK_CHARACTERISTIC_UUID: UUID = UUID.fromString("c776acfe-db36-4956-922d-5f13ddaf8584")
    // Class Upload (Write) -- v1.1 addition, mirrors src/ble_server.cpp's
    // CLASS_UPLOAD_CHAR_UUID. Pushes a trained sound class (name + a 16-
    // float Goertzel centroid computed on-device from recorded samples,
    // see GoertzelFeatures.kt) straight into the belt's flash-backed
    // classifier_store -- no firmware reflash needed to add a class.
    // Needs a negotiated MTU > 23; see BleBeltRepository's requestMtu call.
    val CLASS_UPLOAD_CHARACTERISTIC_UUID: UUID = UUID.fromString("551f1f19-451e-419a-b181-817bb8997fcc")
    // Keyword Upload (Write) -- v1.2 addition, mirrors src/ble_server.cpp's
    // KEYWORD_UPLOAD_CHAR_UUID. Pushes a spoken keyword/name template (a
    // sequence of Goertzel feature blocks, not one static vector -- see
    // KeywordFeatures.kt) into the belt's flash-backed keyword_store.
    // Needs an even larger negotiated MTU than Class Upload (up to 472
    // bytes); BleBeltRepository requests 512.
    val KEYWORD_UPLOAD_CHARACTERISTIC_UUID: UUID = UUID.fromString("88e19205-b3c7-4cfd-8ec2-691dadcdb0ac")
    // Keyword Delete (Write) -- v1.5 addition, mirrors src/ble_server.cpp's
    // KEYWORD_DELETE_CHAR_UUID. Removes one enrolled keyword by name. Added
    // because "Remove from list" used to be local-only (EnrolledKeywordsStore)
    // with no way to actually reach the belt -- a real gap a real user hit.
    val KEYWORD_DELETE_CHARACTERISTIC_UUID: UUID = UUID.fromString("68d90930-8ab9-43b3-b5fb-97dc0c9657e3")

    // Standard BLE-spec UUID for the Client Characteristic Configuration
    // Descriptor -- not something we invent, required to enable notifications.
    val CLIENT_CHARACTERISTIC_CONFIG_UUID: UUID =
        UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    const val STATE_PACKET_SIZE = 13
    const val SETTINGS_PACKET_SIZE = 4
    const val ACK_PACKET_SIZE = 3

    const val ACK_COMMAND_ACKNOWLEDGE_EVENT: Int = 1

    // ---- shared float<->byte quantization (Section 4.3 of BLE_PROTOCOL.md) ----

    fun floatToByte(value: Float): Int = (value.coerceIn(0f, 1f) * 255f).roundToInt().coerceIn(0, 255)

    fun byteToFloat(value: Int): Float = (value and 0xFF) / 255f

    // ---- enum <-> ordinal (Section 4.2) ----
    // Ordinals are fixed by this document and by the firmware's own enums
    // (src/spatial_detector.h, temporal_reasoner.h, priority_engine.h) --
    // deliberately positional, not a lookup by name.

    private val DIRECTION_VALUES = Direction.entries.toTypedArray()
    private val PRIORITY_VALUES = Priority.entries.toTypedArray()
    private val AWARENESS_STATE_VALUES = AwarenessState.entries.toTypedArray()
    private val TREND_VALUES = Trend.entries.toTypedArray()
    private val OPERATING_MODE_VALUES = OperatingMode.entries.toTypedArray()

    fun directionFromOrdinal(ordinal: Int): Direction = DIRECTION_VALUES.getOrElse(ordinal) { Direction.UNKNOWN }
    fun priorityFromOrdinal(ordinal: Int): Priority = PRIORITY_VALUES.getOrElse(ordinal) { Priority.LOW }
    fun awarenessStateFromOrdinal(ordinal: Int): AwarenessState =
        AWARENESS_STATE_VALUES.getOrElse(ordinal) { AwarenessState.BACKGROUND }
    fun trendFromOrdinal(ordinal: Int): Trend = TREND_VALUES.getOrElse(ordinal) { Trend.STABLE }
    fun operatingModeFromOrdinal(ordinal: Int): OperatingMode =
        OPERATING_MODE_VALUES.getOrElse(ordinal) { OperatingMode.HOME }

    // ---- classification label code table (Section 4.1) ----

    private val CLASSIFICATION_LABELS = arrayOf(
        "UNKNOWN",          // 0
        "SPEECH",           // 1
        "ALARM",            // 2
        "SIREN",            // 3
        "HORN",             // 4
        "KNOCK_OR_DOORBELL",// 5
        "PHONE_RINGTONE",   // 6
        "DOG_BARK",         // 7
        "GLASS_BREAK"       // 8
    )

    fun classificationLabelFromCode(code: Int): String = CLASSIFICATION_LABELS.getOrElse(code) { "UNKNOWN" }

    /** Returns 0 (UNKNOWN) for any label this table doesn't recognize. */
    fun classificationCodeFromLabel(label: String): Int {
        val index = CLASSIFICATION_LABELS.indexOf(label)
        return if (index >= 0) index else 0
    }

    // ---- Belt State packet (Section 3) ----

    data class DecodedState(
        val protocolVersion: Int,
        val direction: Direction,
        val directionConfidence: Float,
        val eventScore: Float,
        val eventConfidence: Float,
        val awarenessState: AwarenessState,
        val priority: Priority,
        val sameDirectionRepeats: Int,
        val trend: Trend,
        val classificationLabel: String,
        val eventSeq: Int,
        // Byte 12, was always-0/reserved -- now a one-shot pulse: 0 = no
        // keyword match this packet, 1..N = (keyword_store slot index + 1)
        // of a match on the exact frame that produced this packet. Mirrors
        // keyword_detector.cpp's own one-shot (not persisted) matched flag,
        // see src/ble_server.cpp's bleServerNotifyState().
        val keywordMatchSlot: Int
    )

    /** Returns null if [bytes] is too short to be a valid state packet. */
    fun decodeState(bytes: ByteArray): DecodedState? {
        if (bytes.size < STATE_PACKET_SIZE) return null
        val u = { i: Int -> bytes[i].toInt() and 0xFF }
        return DecodedState(
            protocolVersion = u(0),
            direction = directionFromOrdinal(u(1)),
            directionConfidence = byteToFloat(u(2)),
            eventScore = byteToFloat(u(3)),
            eventConfidence = byteToFloat(u(4)),
            awarenessState = awarenessStateFromOrdinal(u(5)),
            priority = priorityFromOrdinal(u(6)),
            sameDirectionRepeats = u(7),
            trend = trendFromOrdinal(u(8)),
            classificationLabel = classificationLabelFromCode(u(9)),
            eventSeq = u(10) or (u(11) shl 8),
            keywordMatchSlot = u(12)
        )
    }

    // ---- Settings packet (Section 5) ----

    data class DecodedSettings(
        val protocolVersion: Int,
        val operatingMode: OperatingMode,
        val vibrationIntensity: Float,
        val sensitivity: Float
    )

    fun decodeSettings(bytes: ByteArray): DecodedSettings? {
        if (bytes.size < SETTINGS_PACKET_SIZE) return null
        val u = { i: Int -> bytes[i].toInt() and 0xFF }
        return DecodedSettings(
            protocolVersion = u(0),
            operatingMode = operatingModeFromOrdinal(u(1)),
            vibrationIntensity = byteToFloat(u(2)),
            sensitivity = byteToFloat(u(3))
        )
    }

    fun encodeSettings(operatingMode: OperatingMode, vibrationIntensity: Float, sensitivity: Float): ByteArray {
        return byteArrayOf(
            PROTOCOL_VERSION.toByte(),
            operatingMode.ordinal.toByte(),
            floatToByte(vibrationIntensity).toByte(),
            floatToByte(sensitivity).toByte()
        )
    }

    // ---- Acknowledge packet (Section 6) ----

    fun encodeAcknowledge(eventSeq: Int): ByteArray {
        val seq = eventSeq and 0xFFFF
        return byteArrayOf(
            ACK_COMMAND_ACKNOWLEDGE_EVENT.toByte(),
            (seq and 0xFF).toByte(),
            ((seq shr 8) and 0xFF).toByte()
        )
    }

    // ---- Class Upload packet (v1.1 addition) ----
    // Wire format: [nameLen:1][name:nameLen ASCII][centroid:64 bytes, 16 LE float32]
    // -- mirrors src/ble_server.cpp's ClassUploadCallbacks::onWrite exactly.

    const val CLASS_UPLOAD_MAX_NAME_LEN = 23 // firmware's CLASSIFIER_MAX_NAME_LEN(24) - 1, for the null terminator

    /** Returns null if [name] doesn't fit or [centroid] isn't the expected length. */
    fun encodeClassUpload(name: String, centroid: FloatArray): ByteArray? {
        val nameBytes = name.encodeToByteArray()
        if (nameBytes.isEmpty() || nameBytes.size > CLASS_UPLOAD_MAX_NAME_LEN) return null
        if (centroid.size != 16) return null

        val buffer = java.nio.ByteBuffer.allocate(1 + nameBytes.size + centroid.size * 4)
            .order(java.nio.ByteOrder.LITTLE_ENDIAN)
        buffer.put(nameBytes.size.toByte())
        buffer.put(nameBytes)
        for (v in centroid) buffer.putFloat(v)
        return buffer.array()
    }

    // ---- Keyword Upload packets (v1.6, chunked, variable-length, multi-template) ----
    // One write per segment, NOT one write of the whole template in a
    // single packet: a single large write was this project's first
    // real-hardware BLE test and consistently killed the connection
    // outright (status=133) despite a correctly negotiated 512 MTU -- see
    // src/ble_server.cpp's KEYWORD_UPLOAD_CHAR_UUID comment for the full
    // story. Splitting into small per-segment writes sidesteps it.
    //
    // v1.6: gained a leading templateIndex byte -- each of a word's
    // KEYWORD_TEMPLATES_PER_WORD enrollment recordings is uploaded as its
    // own separate template now (BleBeltRepository.uploadKeyword loops and
    // sends one full chunked upload per recording), instead of the phone
    // picking one "representative" recording (originally an averaged
    // vector, later a medoid pick) before ever uploading. That was flagged
    // as the single highest-priority fix after live testing kept showing
    // one-template-per-word + a single global threshold struggling with
    // near-duplicate keywords -- see keyword_store.h's history comment.
    // Wire format per segment: [templateIndex:1][segmentIndex:1]
    // [totalSegments:1][nameLen:1][name:nameLen ASCII][segment:
    // KEYWORD_FEATURE_COUNT floats, LE float32] -- mirrors
    // src/ble_server.cpp's KeywordUploadCallbacks::onWrite exactly. Segments
    // of the same (name, templateIndex) must be sent in order and the
    // caller must wait for each write's real result before sending the
    // next (BleBeltRepository does, via writeCharacteristicWithRetry) --
    // the firmware has no reassembly buffer for out-of-order or
    // interleaved segments.

    const val KEYWORD_UPLOAD_MAX_NAME_LEN = 23 // firmware's KEYWORD_MAX_NAME_LEN(24) - 1
    const val KEYWORD_FEATURE_COUNT = 13        // must match src/mfcc_dsp.h's MFCC_COUNT
    // 60/40 -- shrunk 2026-09-16 after discovering the belt's NVS partition
    // (20480 bytes) couldn't actually hold KEYWORD_MAX_COUNT(2) keywords at
    // the old 60/5 sizing (~31KB worst case) -- see src/keyword_store.h's
    // history comment for the full story. Must match src/keyword_store.h's
    // KEYWORD_MAX_TEMPLATE_FRAMES exactly.
    const val KEYWORD_MAX_TEMPLATE_FRAMES = 40
    const val KEYWORD_TEMPLATES_PER_WORD = 3    // must match src/keyword_store.h's KEYWORD_TEMPLATES_PER_WORD

    /**
     * Returns the per-segment payloads (one per MFCC frame in [template],
     * [frameCount] of them) for template slot [templateIndex] of [name], to
     * write in order, or null if [name] doesn't fit, [templateIndex] is out
     * of range, or [frameCount] is out of range.
     */
    fun encodeKeywordUploadSegments(name: String, templateIndex: Int, template: FloatArray, frameCount: Int): List<ByteArray>? {
        val nameBytes = name.encodeToByteArray()
        if (nameBytes.isEmpty() || nameBytes.size > KEYWORD_UPLOAD_MAX_NAME_LEN) return null
        if (templateIndex < 0 || templateIndex >= KEYWORD_TEMPLATES_PER_WORD) return null
        if (frameCount <= 0 || frameCount > KEYWORD_MAX_TEMPLATE_FRAMES) return null
        if (template.size < frameCount * KEYWORD_FEATURE_COUNT) return null

        return (0 until frameCount).map { segmentIndex ->
            val buffer = java.nio.ByteBuffer.allocate(4 + nameBytes.size + KEYWORD_FEATURE_COUNT * 4)
                .order(java.nio.ByteOrder.LITTLE_ENDIAN)
            buffer.put(templateIndex.toByte())
            buffer.put(segmentIndex.toByte())
            buffer.put(frameCount.toByte())
            buffer.put(nameBytes.size.toByte())
            buffer.put(nameBytes)
            val offset = segmentIndex * KEYWORD_FEATURE_COUNT
            for (i in 0 until KEYWORD_FEATURE_COUNT) buffer.putFloat(template[offset + i])
            buffer.array()
        }
    }

    // ---- Keyword Delete packet (v1.5 addition) ----
    // Wire format: [nameLen:1][name:nameLen ASCII] -- mirrors
    // src/ble_server.cpp's KeywordDeleteCallbacks::onWrite exactly.

    /** Returns null if [name] doesn't fit. */
    fun encodeKeywordDelete(name: String): ByteArray? {
        val nameBytes = name.encodeToByteArray()
        if (nameBytes.isEmpty() || nameBytes.size > KEYWORD_UPLOAD_MAX_NAME_LEN) return null
        val buffer = java.nio.ByteBuffer.allocate(1 + nameBytes.size)
        buffer.put(nameBytes.size.toByte())
        buffer.put(nameBytes)
        return buffer.array()
    }
}
