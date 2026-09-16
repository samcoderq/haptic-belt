package com.hapticbelt.app.data.ble

import android.Manifest
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.ParcelUuid
import android.util.Log
import androidx.core.content.ContextCompat
import com.hapticbelt.app.data.AwarenessState
import com.hapticbelt.app.data.BeltEvent
import com.hapticbelt.app.data.BeltRepository
import com.hapticbelt.app.data.BeltSettings
import com.hapticbelt.app.data.BeltState
import com.hapticbelt.app.data.Direction
import com.hapticbelt.app.data.Priority
import com.hapticbelt.app.data.Trend
import com.hapticbelt.app.data.notifications.CapturedNotification
import com.hapticbelt.app.data.training.EnrolledKeywordsStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.CancellableContinuation
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeoutOrNull

/**
 * Real BLE-backed [BeltRepository], implementing the wire format documented
 * in `android_app/BLE_PROTOCOL.md` and [BleProtocol]. Verified against real
 * firmware: scans, connects, negotiates a 512 MTU, and receives live Belt
 * State packets. Scanning matches on device name OR service UUID rather
 * than relying solely on the OS-level `ScanFilter` (that filter alone
 * matched nothing on the first real test even with correct advertisement
 * data -- flaky on at least one real BLE stack). Scanning/connecting/parsing
 * is written defensively (missing permissions, adapter off, no matching
 * device, mid-connection drop) so the app doesn't crash if the belt is out
 * of range or powered off, and reconnects on its own once it's found again.
 *
 * No tested range/latency/throughput numbers exist yet beyond "works at
 * close range" -- see BLE_PROTOCOL.md Section 8.
 */
class BleBeltRepository(private val context: Context) : BeltRepository {

    private val _beltState = MutableStateFlow(BeltState())
    override val beltState: StateFlow<BeltState> = _beltState.asStateFlow()

    private val _eventHistory = MutableStateFlow<List<BeltEvent>>(emptyList())
    override val eventHistory: StateFlow<List<BeltEvent>> = _eventHistory.asStateFlow()

    private val _settings = MutableStateFlow(BeltSettings())
    override val settings: StateFlow<BeltSettings> = _settings.asStateFlow()

    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    private var connectionLoopJob: Job? = null
    private var running = false

    @Volatile private var bluetoothGatt: BluetoothGatt? = null

    // eventSeq of the most recently received Belt State packet. Doubles as
    // (a) what acknowledgeCurrentEvent() echoes back on the Ack
    // characteristic, and (b) the dedup key for deciding a notification
    // represents a *new* onset worth adding to eventHistory. -1 so the very
    // first real packet (eventSeq 0) still counts as new.
    @Volatile private var lastKnownEventSeq: Int = -1
    private var nextLocalEventId = 1L

    // BluetoothGatt does not support concurrent operations -- issuing a
    // second write before the first one's onCharacteristicWrite callback
    // returns is a well-known source of a generic status=133 failure, and is
    // exactly what this project's first live test hit on the largest write
    // this protocol ever does (Keyword Upload, ~472 bytes): the write
    // reported failure, but the caller (uploadKeyword) never checked and the
    // UI claimed "Sent" anyway. This mutex serializes every GATT write
    // through writeCharacteristicAwait() below so callers can get a real
    // success/failure result instead of firing-and-forgetting.
    private val gattOpMutex = Mutex()
    @Volatile private var pendingWriteContinuation: CancellableContinuation<Int>? = null

    private val bluetoothManager: BluetoothManager? by lazy {
        context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
    }

    override fun start() {
        if (running) return
        running = true
        connectionLoopJob = scope.launch {
            while (isActive && running) {
                if (bluetoothGatt == null) {
                    attemptConnectOnce()
                }
                delay(RECONNECT_POLL_INTERVAL_MS)
            }
        }
    }

    override fun stop() {
        running = false
        connectionLoopJob?.cancel()
        connectionLoopJob = null
        closeGatt()
        _beltState.value = _beltState.value.copy(connected = false)
    }

    override fun updateSettings(update: (BeltSettings) -> BeltSettings) {
        val updated = update(_settings.value)
        _settings.value = updated
        writeSettingsToDevice(updated)
    }

    override fun acknowledgeCurrentEvent() {
        if (bluetoothGatt != null && hasConnectPermission()) {
            // Routed through the same serialized/retried path as uploads
            // (see writeCharacteristicWithRetry's comment) even though the
            // result is discarded here -- keeps this from colliding with an
            // in-flight upload write, which is the failure this project's
            // first live test actually hit.
            scope.launch { writeCharacteristicWithRetry(BleProtocol.ACK_CHARACTERISTIC_UUID, BleProtocol.encodeAcknowledge(lastKnownEventSeq)) }
        } else {
            Log.w(TAG, "acknowledgeCurrentEvent(): no connected belt to write the ack to")
        }
        // Optimistic local update so the UI clears the alert immediately
        // rather than waiting on a round trip that, right now, has nothing
        // to round-trip to. A real device could still notify a conflicting
        // state afterward, which would simply overwrite this.
        _beltState.value = _beltState.value.copy(priority = Priority.LOW, state = AwarenessState.COOLDOWN)
    }

    override fun simulateCriticalEvent() {
        // Deliberately a no-op here, unlike MockBeltRepository -- fabricating
        // a CRITICAL event on the *real* repository would misrepresent what
        // the belt actually reported. Use Mock mode to exercise this flow.
        Log.w(TAG, "simulateCriticalEvent() has no effect on BleBeltRepository; it only fabricates data in MockBeltRepository")
    }

    override fun handleNotificationEvent(event: CapturedNotification) {
        // Unlike simulateCriticalEvent(), this is NOT fabricated data -- a
        // real notification really was posted and really was classified.
        // What's still missing is a way to push it onward to real firmware:
        // BLE_PROTOCOL.md has no packet type for "phone-originated event" (it
        // only ever describes belt -> phone Belt State packets), so this only
        // updates the app's own visible state for now. A real belt will never
        // buzz from this until that gap is closed.
        Log.i(TAG, "handleNotificationEvent(${event.appLabel}, ${event.pattern.priority}): updating in-app state only -- no BLE packet type exists yet to relay this to firmware")
        val newState = BeltState(
            connected = _beltState.value.connected,
            direction = Direction.UNKNOWN,
            directionConfidence = 0f,
            eventScore = 1f,
            eventConfidence = 1f,
            state = AwarenessState.EVENT,
            priority = event.pattern.priority,
            sameDirectionRepeats = 0,
            trend = Trend.STABLE,
            classificationLabel = "PHONE: ${event.appLabel}",
            lastEventAtMillis = event.timestampMillis
        )
        _beltState.value = newState

        _eventHistory.value = (listOf(
            BeltEvent(
                id = nextLocalEventId++,
                timestampMillis = event.timestampMillis,
                direction = Direction.UNKNOWN,
                directionConfidence = 0f,
                priority = event.pattern.priority,
                eventConfidence = 1f,
                classificationLabel = "PHONE: ${event.appLabel}"
            )
        ) + _eventHistory.value).take(50)
    }

    override suspend fun uploadTrainedClass(className: String, centroid: FloatArray): Boolean {
        val payload = BleProtocol.encodeClassUpload(className, centroid)
        if (payload == null) {
            Log.w(TAG, "uploadTrainedClass('$className'): name too long or centroid wrong size, not sending")
            return false
        }
        return writeCharacteristicWithRetry(BleProtocol.CLASS_UPLOAD_CHARACTERISTIC_UUID, payload)
    }

    override suspend fun uploadKeyword(keywordName: String, templates: List<Pair<FloatArray, Int>>): Boolean {
        // One full chunked upload per template (each an independent
        // enrollment recording) -- see BeltRepository.kt's interface
        // comment for why these are no longer reduced to one
        // "representative" recording before upload.
        for ((templateIndex, templateAndFrames) in templates.withIndex()) {
            val (template, frameCount) = templateAndFrames
            val segments = BleProtocol.encodeKeywordUploadSegments(keywordName, templateIndex, template, frameCount)
            if (segments == null) {
                Log.w(TAG, "uploadKeyword('$keywordName'): template $templateIndex -- name too long or frame count out of range, not sending")
                return false
            }
            // Sent one at a time, each awaited (with its own retries) before
            // the next is sent -- see encodeKeywordUploadSegments's comment
            // for why this is chunked at all, and ble_server.cpp's
            // KeywordUploadCallbacks for why segments must arrive strictly
            // in order.
            for ((index, segment) in segments.withIndex()) {
                val ok = writeCharacteristicWithRetry(BleProtocol.KEYWORD_UPLOAD_CHARACTERISTIC_UUID, segment)
                if (!ok) {
                    Log.w(TAG, "uploadKeyword('$keywordName'): template $templateIndex segment $index/${segments.size} failed, aborting upload")
                    return false
                }
            }
        }
        return true
    }

    override suspend fun deleteKeyword(keywordName: String): Boolean {
        val payload = BleProtocol.encodeKeywordDelete(keywordName)
        if (payload == null) {
            Log.w(TAG, "deleteKeyword('$keywordName'): name too long, not sending")
            return false
        }
        return writeCharacteristicWithRetry(BleProtocol.KEYWORD_DELETE_CHARACTERISTIC_UUID, payload)
    }

    // ---------------------------------------------------------------------
    // Connection lifecycle
    // ---------------------------------------------------------------------

    private suspend fun attemptConnectOnce() {
        if (!hasScanPermission()) {
            Log.w(TAG, "Skipping scan: missing scan permission (BLUETOOTH_SCAN / ACCESS_FINE_LOCATION)")
            return
        }
        val adapter = bluetoothManager?.adapter
        if (adapter == null || !adapter.isEnabled) {
            Log.w(TAG, "Skipping scan: Bluetooth adapter missing or disabled")
            return
        }
        val device = scanForBeltOnce(adapter.bluetoothLeScanner, SCAN_TIMEOUT_MS) ?: return
        if (!hasConnectPermission()) {
            Log.w(TAG, "Found a candidate device but missing connect permission (BLUETOOTH_CONNECT)")
            return
        }
        try {
            device.connectGatt(context, false, gattCallback)
        } catch (e: SecurityException) {
            Log.w(TAG, "connectGatt() denied: ${e.message}")
        }
    }

    // Deliberately unfiltered (no ScanFilter.setServiceUuid()) and matched by
    // hand instead: that filter matched nothing on the first real-hardware
    // test against a Samsung phone even though the firmware's advertisement
    // data was verified correct (NimBLE keeps the 128-bit service UUID in the
    // primary adv packet, only the name overflows to scan response) -- a
    // known-flaky combination on some OEM BLE stacks. Matching on device name
    // OR the service UUID (whichever the callback's ScanRecord actually
    // exposes) is more robust than depending on the OS-level filter.
    private suspend fun scanForBeltOnce(
        scanner: android.bluetooth.le.BluetoothLeScanner?,
        timeoutMs: Long
    ): BluetoothDevice? {
        if (scanner == null) return null
        return withTimeoutOrNull(timeoutMs) {
            suspendCancellableCoroutine { cont ->
                val settings = ScanSettings.Builder()
                    .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                    .build()
                val callback = object : ScanCallback() {
                    override fun onScanResult(callbackType: Int, result: ScanResult) {
                        val record = result.scanRecord
                        val name = result.device.let {
                            try { if (hasConnectPermission()) it.name else null } catch (e: SecurityException) { null }
                        } ?: record?.deviceName
                        val uuidMatch = record?.serviceUuids?.contains(ParcelUuid(BleProtocol.SERVICE_UUID)) == true
                        if ((name == BleProtocol.DEVICE_NAME || uuidMatch) && cont.isActive) {
                            Log.i(TAG, "Found belt: name=$name uuidMatch=$uuidMatch rssi=${result.rssi}")
                            cont.resume(result.device, onCancellation = null)
                            stopScanQuietly(scanner, this)
                        }
                    }

                    override fun onScanFailed(errorCode: Int) {
                        Log.w(TAG, "BLE scan failed: errorCode=$errorCode")
                        if (cont.isActive) cont.resume(null, onCancellation = null)
                    }
                }
                cont.invokeOnCancellation { stopScanQuietly(scanner, callback) }
                try {
                    scanner.startScan(null, settings, callback)
                } catch (e: SecurityException) {
                    if (cont.isActive) cont.resume(null, onCancellation = null)
                }
            }
        }
    }

    private fun stopScanQuietly(scanner: android.bluetooth.le.BluetoothLeScanner, callback: ScanCallback) {
        try {
            scanner.stopScan(callback)
        } catch (e: SecurityException) {
            // Permission was revoked mid-scan; nothing to clean up beyond this.
        }
    }

    private fun closeGatt() {
        val gatt = bluetoothGatt ?: return
        bluetoothGatt = null
        try {
            gatt.disconnect()
            gatt.close()
        } catch (e: SecurityException) {
            // Nothing more we can do without the permission that was revoked.
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    bluetoothGatt = gatt
                    try {
                        // Class Upload needs more than the default 23-byte MTU;
                        // Keyword Upload needs even more (a template is a
                        // sequence of feature blocks, not one vector, up to
                        // ~472 bytes) -- 512 covers both, requested here once
                        // per connection rather than gating on it.
                        gatt.requestMtu(512)
                        gatt.discoverServices()
                    } catch (e: SecurityException) {
                        Log.w(TAG, "discoverServices()/requestMtu() denied: ${e.message}")
                    }
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    _beltState.value = _beltState.value.copy(connected = false)
                    try {
                        gatt.close()
                    } catch (e: SecurityException) {
                        // ignore
                    }
                    if (bluetoothGatt === gatt) bluetoothGatt = null
                    // The connection loop in start() notices bluetoothGatt == null
                    // on its next tick and resumes scanning -- no separate
                    // retry logic needed here.
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.w(TAG, "onServicesDiscovered failed: status=$status")
                return
            }
            val service = gatt.getService(BleProtocol.SERVICE_UUID)
            if (service == null) {
                Log.w(TAG, "Connected device does not expose the Haptic Belt service (${BleProtocol.SERVICE_UUID})")
                return
            }
            try {
                service.getCharacteristic(BleProtocol.STATE_CHARACTERISTIC_UUID)?.let { stateChar ->
                    enableNotifications(gatt, stateChar)
                    gatt.readCharacteristic(stateChar)
                }
                service.getCharacteristic(BleProtocol.SETTINGS_CHARACTERISTIC_UUID)?.let { settingsChar ->
                    gatt.readCharacteristic(settingsChar)
                }
            } catch (e: SecurityException) {
                Log.w(TAG, "Post-discovery setup denied: ${e.message}")
            }
            _beltState.value = _beltState.value.copy(connected = true)
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            Log.i(TAG, "MTU negotiated: $mtu (requested 512, status=$status)")
        }

        @Suppress("DEPRECATION")
        override fun onCharacteristicRead(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            if (status != BluetoothGatt.GATT_SUCCESS) return
            handleCharacteristicValue(characteristic.uuid, characteristic.value)
        }

        @Suppress("DEPRECATION")
        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            handleCharacteristicValue(characteristic.uuid, characteristic.value)
        }

        override fun onCharacteristicWrite(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.w(TAG, "Write to ${characteristic.uuid} failed: status=$status")
            }
            val cont = pendingWriteContinuation
            pendingWriteContinuation = null
            if (cont?.isActive == true) cont.resume(status, onCancellation = null)
        }
    }

    private fun enableNotifications(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
        gatt.setCharacteristicNotification(characteristic, true)
        val descriptor = characteristic.getDescriptor(BleProtocol.CLIENT_CHARACTERISTIC_CONFIG_UUID) ?: return
        writeDescriptorCompat(gatt, descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
    }

    // ---------------------------------------------------------------------
    // Parsing incoming data
    // ---------------------------------------------------------------------

    private fun handleCharacteristicValue(uuid: java.util.UUID, value: ByteArray?) {
        if (value == null) return
        when (uuid) {
            BleProtocol.STATE_CHARACTERISTIC_UUID -> handleStatePacket(value)
            BleProtocol.SETTINGS_CHARACTERISTIC_UUID -> handleSettingsPacket(value)
        }
    }

    private fun handleStatePacket(value: ByteArray) {
        val decoded = BleProtocol.decodeState(value) ?: run {
            Log.w(TAG, "Belt State packet too short: ${value.size} bytes")
            return
        }
        if (decoded.protocolVersion != BleProtocol.PROTOCOL_VERSION) {
            Log.w(TAG, "Belt State packet protocolVersion=${decoded.protocolVersion}, expected ${BleProtocol.PROTOCOL_VERSION}")
        }

        val isNewOnset = decoded.eventSeq != lastKnownEventSeq
        lastKnownEventSeq = decoded.eventSeq

        val previous = _beltState.value
        // keywordMatchSlot is a one-shot pulse (0 on every frame except the
        // exact one a match fires), not a persisted state, so a nonzero
        // value here always means "a match just happened this packet" --
        // no eventSeq-style dedup needed, unlike the EVENT/onset handling
        // below which has to tell a repeat notification from a new one.
        val keywordJustMatched = decoded.keywordMatchSlot > 0
        val keywordName = if (keywordJustMatched) {
            EnrolledKeywordsStore.nameForSlot(context, decoded.keywordMatchSlot - 1)
        } else null
        if (keywordJustMatched) {
            Log.i(TAG, "Keyword match: slot=${decoded.keywordMatchSlot} name=${keywordName ?: "(unknown -- no local record for this slot)"}")
        }

        val newState = BeltState(
            connected = true,
            direction = decoded.direction,
            directionConfidence = decoded.directionConfidence,
            eventScore = decoded.eventScore,
            eventConfidence = decoded.eventConfidence,
            state = decoded.awarenessState,
            priority = decoded.priority,
            sameDirectionRepeats = decoded.sameDirectionRepeats,
            trend = decoded.trend,
            classificationLabel = decoded.classificationLabel,
            lastEventAtMillis = if (isNewOnset && decoded.awarenessState == AwarenessState.EVENT) {
                System.currentTimeMillis()
            } else {
                previous.lastEventAtMillis
            },
            lastKeywordMatchName = if (keywordJustMatched) (keywordName ?: "Unknown keyword (slot ${decoded.keywordMatchSlot})") else previous.lastKeywordMatchName,
            lastKeywordMatchAtMillis = if (keywordJustMatched) System.currentTimeMillis() else previous.lastKeywordMatchAtMillis
        )
        _beltState.value = newState

        if (isNewOnset && decoded.awarenessState == AwarenessState.EVENT) {
            val event = BeltEvent(
                id = nextLocalEventId++,
                timestampMillis = System.currentTimeMillis(),
                direction = decoded.direction,
                directionConfidence = decoded.directionConfidence,
                priority = decoded.priority,
                eventConfidence = decoded.eventConfidence,
                classificationLabel = decoded.classificationLabel
            )
            _eventHistory.value = (listOf(event) + _eventHistory.value).take(50)
        }
    }

    private fun handleSettingsPacket(value: ByteArray) {
        val decoded = BleProtocol.decodeSettings(value) ?: run {
            Log.w(TAG, "Settings packet too short: ${value.size} bytes")
            return
        }
        // Only the fields the device actually owns (Section 5 of
        // BLE_PROTOCOL.md) -- phone-only fields like emergency contact info
        // are left untouched.
        _settings.value = _settings.value.copy(
            operatingMode = decoded.operatingMode,
            vibrationIntensity = decoded.vibrationIntensity,
            sensitivity = decoded.sensitivity
        )
    }

    // ---------------------------------------------------------------------
    // Writing outgoing data
    // ---------------------------------------------------------------------

    private fun writeSettingsToDevice(settings: BeltSettings) {
        if (bluetoothGatt == null || !hasConnectPermission()) return
        val payload = BleProtocol.encodeSettings(settings.operatingMode, settings.vibrationIntensity, settings.sensitivity)
        scope.launch { writeCharacteristicWithRetry(BleProtocol.SETTINGS_CHARACTERISTIC_UUID, payload) }
    }

    @Suppress("DEPRECATION")
    private fun writeCharacteristicCompat(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray) {
        try {
            characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            characteristic.value = value
            gatt.writeCharacteristic(characteristic)
        } catch (e: SecurityException) {
            Log.w(TAG, "writeCharacteristic(${characteristic.uuid}) denied: ${e.message}")
        }
    }

    // Single GATT write attempt, awaiting the real onCharacteristicWrite
    // status instead of firing and forgetting, serialized against every
    // other GATT write via gattOpMutex (see its comment).
    private suspend fun writeCharacteristicOnce(
        gatt: BluetoothGatt,
        characteristic: BluetoothGattCharacteristic,
        value: ByteArray
    ): Int? = gattOpMutex.withLock {
        withTimeoutOrNull(WRITE_TIMEOUT_MS) {
            suspendCancellableCoroutine<Int> { cont ->
                pendingWriteContinuation = cont
                cont.invokeOnCancellation { if (pendingWriteContinuation === cont) pendingWriteContinuation = null }
                writeCharacteristicCompat(gatt, characteristic, value)
            }
        }
    }

    // Retries by RE-resolving bluetoothGatt/the characteristic fresh on
    // every attempt, not just once -- a real bug this project's first live
    // keyword-upload test hit: the write's status=133 failure took the
    // connection down with it, a reconnect happened mid-retry, but the old
    // retry loop kept reusing the original (now closed) BluetoothGatt
    // object, so every subsequent attempt just silently timed out instead
    // of ever getting a chance against the new connection. Used by
    // uploadKeyword/uploadTrainedClass, where the caller (and the
    // enrollment UI) needs to know whether this genuinely reached the belt,
    // not just that a write was attempted.
    private suspend fun writeCharacteristicWithRetry(
        characteristicUuid: java.util.UUID,
        value: ByteArray,
        maxAttempts: Int = 3
    ): Boolean {
        repeat(maxAttempts) { attempt ->
            val gatt = bluetoothGatt
            val characteristic = gatt?.getService(BleProtocol.SERVICE_UUID)?.getCharacteristic(characteristicUuid)
            if (gatt != null && characteristic != null && hasConnectPermission()) {
                val status = writeCharacteristicOnce(gatt, characteristic, value)
                if (status == BluetoothGatt.GATT_SUCCESS) return true
                Log.w(TAG, "writeCharacteristicWithRetry($characteristicUuid) attempt ${attempt + 1}/$maxAttempts: status=$status")
            } else {
                Log.w(TAG, "writeCharacteristicWithRetry($characteristicUuid) attempt ${attempt + 1}/$maxAttempts: not connected")
            }
            if (attempt < maxAttempts - 1) delay(WRITE_RETRY_DELAY_MS)
        }
        return false
    }

    @Suppress("DEPRECATION")
    private fun writeDescriptorCompat(gatt: BluetoothGatt, descriptor: BluetoothGattDescriptor, value: ByteArray) {
        try {
            descriptor.value = value
            gatt.writeDescriptor(descriptor)
        } catch (e: SecurityException) {
            Log.w(TAG, "writeDescriptor(${descriptor.uuid}) denied: ${e.message}")
        }
    }

    // ---------------------------------------------------------------------
    // Permissions
    // ---------------------------------------------------------------------

    private fun hasScanPermission(): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED
        } else {
            // Pre-Android 12, BLE scanning requires location permission, not
            // a Bluetooth-specific one.
            ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
        }
    }

    private fun hasConnectPermission(): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
        } else {
            // BLUETOOTH_CONNECT didn't exist before API 31; the pre-31
            // BLUETOOTH permission is a normal (install-time) permission,
            // already declared in the manifest, no runtime grant needed.
            true
        }
    }

    companion object {
        private const val TAG = "BleBeltRepository"
        private const val SCAN_TIMEOUT_MS = 10_000L
        private const val RECONNECT_POLL_INTERVAL_MS = 5_000L
        private const val WRITE_TIMEOUT_MS = 5_000L
        private const val WRITE_RETRY_DELAY_MS = 200L
    }
}
