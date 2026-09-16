package com.hapticbelt.app.data

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import com.hapticbelt.app.data.notifications.CapturedNotification
import kotlinx.coroutines.launch
import kotlin.random.Random

// The seam where real BLE gets wired in later -- everything in the UI
// layer talks to this interface, not to a concrete data source, so
// swapping Mock -> real BLE touches this file only.
interface BeltRepository {
    val beltState: StateFlow<BeltState>
    val eventHistory: StateFlow<List<BeltEvent>>
    val settings: StateFlow<BeltSettings>

    fun start()
    fun stop()
    fun updateSettings(update: (BeltSettings) -> BeltSettings)

    // Called by the UI when the user acknowledges a CRITICAL alert.
    fun acknowledgeCurrentEvent()

    // Test-only hook so the emergency flow can be exercised on demand
    // without waiting for the random simulator to roll CRITICAL.
    fun simulateCriticalEvent()

    // Called by MainViewModel when NotificationBridge captures and classifies
    // a real phone notification. Phone notifications carry no spatial info,
    // so direction is always UNKNOWN here -- this reflects the belt's visible
    // state and event history exactly like a real sound event would, but
    // cannot yet cause an actual vibration; that needs real motors and (for
    // BleBeltRepository) a way to push a phone-originated event over BLE to
    // firmware that has no such packet type yet.
    fun handleNotificationEvent(event: CapturedNotification)

    // Called by MainViewModel when NotificationCaptureService sees a
    // previously-captured call notification actually removed (answered,
    // declined, or the call ended) -- clears BeltState.activeCallLabel so
    // the Dashboard's Phone card stops showing it as live. Only clears if
    // [packageName] matches the currently-active call, so an unrelated
    // notification from a different app being dismissed can't clear it.
    fun handleCallEnded(packageName: String)

    // Called by SoundTrainingScreen once a class has enough recorded
    // samples: className + the centroid computed on-device from them
    // (GoertzelFeatures.computeCentroid over each sample's extractFeatures).
    // Suspends until the real BLE write result is known (with a couple of
    // internal retries on a transient failure -- see BleBeltRepository's
    // writeCharacteristicAwait) and returns whether it genuinely reached the
    // belt. NOT fire-and-forget: an earlier version of this was, and it let
    // the enrollment UI claim "Sent" on a write that had actually failed
    // (status=133) on this project's first live hardware test.
    suspend fun uploadTrainedClass(className: String, centroid: FloatArray): Boolean

    // Called by KeywordEnrollScreen once "say it 5 times" enrollment
    // finishes: keywordName + one (template MFCC floats, frameCount) pair
    // PER successfully-captured recording (see KeywordFeatures.extractTemplate),
    // up to KEYWORD_TEMPLATES_PER_WORD. Each is uploaded and stored as its
    // own separate template -- no longer reduced to one "representative"
    // recording before upload (originally an average, later a medoid pick)
    // -- multiple alternate pronunciations of the same word turned out to
    // matter more than picking one supposedly-best recording. Real result
    // means every template in the list was confirmed uploaded; a partial
    // failure still leaves whichever templates DID succeed stored on the
    // belt (each is its own independent BLE upload).
    suspend fun uploadKeyword(keywordName: String, templates: List<Pair<FloatArray, Int>>): Boolean

    // Called by KeywordEnrollScreen's "Remove from list" action. Real
    // result contract same as the uploads above -- added because removing
    // a keyword used to only ever touch EnrolledKeywordsStore (this phone's
    // own local record), never the belt itself, which looked like deletion
    // but wasn't: the belt had no way to be told to forget a keyword at
    // all until this existed.
    suspend fun deleteKeyword(keywordName: String): Boolean
}

/**
 * Simulates belt behavior so the app is fully interactive before any real
 * hardware is wired up. Cycles through BACKGROUND -> NOTICE -> EVENT ->
 * COOLDOWN with randomized direction/priority, occasionally producing a
 * CRITICAL event to exercise the emergency-contact flow end to end.
 */
class MockBeltRepository : BeltRepository {

    private val _beltState = MutableStateFlow(BeltState())
    override val beltState: StateFlow<BeltState> = _beltState.asStateFlow()

    private val _eventHistory = MutableStateFlow<List<BeltEvent>>(emptyList())
    override val eventHistory: StateFlow<List<BeltEvent>> = _eventHistory.asStateFlow()

    private val _settings = MutableStateFlow(BeltSettings())
    override val settings: StateFlow<BeltSettings> = _settings.asStateFlow()

    private val scope = CoroutineScope(Dispatchers.Default)
    private var job: Job? = null
    private var nextEventId = 1L

    override fun start() {
        if (job?.isActive == true) return
        job = scope.launch {
            delay(800) // simulate BLE connect handshake
            _beltState.value = _beltState.value.copy(connected = true)

            while (true) {
                delay(Random.nextLong(2500, 6000))
                simulateOneEvent()
                delay(1200)
                settleToBackground()
            }
        }
    }

    override fun stop() {
        job?.cancel()
    }

    override fun updateSettings(update: (BeltSettings) -> BeltSettings) {
        _settings.value = update(_settings.value)
    }

    override fun acknowledgeCurrentEvent() {
        _beltState.value = _beltState.value.copy(priority = Priority.LOW, state = AwarenessState.COOLDOWN)
    }

    override fun simulateCriticalEvent() {
        _beltState.value = _beltState.value.copy(
            connected = true,
            direction = Direction.entries.filter { it != Direction.UNKNOWN }.random(),
            directionConfidence = 0.9f,
            eventScore = 1.0f,
            eventConfidence = 0.9f,
            state = AwarenessState.EVENT,
            priority = Priority.CRITICAL,
            lastEventAtMillis = System.currentTimeMillis()
        )
    }

    private fun simulateOneEvent() {
        val directions = Direction.entries.filter { it != Direction.UNKNOWN }
        val direction = directions.random()
        val priorityRoll = Random.nextFloat()
        val priority = when {
            priorityRoll > 0.93f -> Priority.CRITICAL
            priorityRoll > 0.75f -> Priority.HIGH
            priorityRoll > 0.45f -> Priority.MEDIUM
            else -> Priority.LOW
        }
        val confidence = Random.nextFloat().coerceIn(0.3f, 1.0f)
        val eventConfidence = Random.nextFloat().coerceIn(0.4f, 1.0f)
        val labels = listOf("UNKNOWN", "UNKNOWN", "UNKNOWN") // classifier isn't real yet -- don't fake specificity
        val label = labels.random()

        _beltState.value = _beltState.value.copy(
            connected = true,
            direction = direction,
            directionConfidence = confidence,
            eventScore = Random.nextFloat().coerceIn(0.3f, 1.0f),
            eventConfidence = eventConfidence,
            state = AwarenessState.EVENT,
            priority = priority,
            sameDirectionRepeats = Random.nextInt(0, 4),
            trend = Trend.entries.random(),
            classificationLabel = label,
            lastEventAtMillis = System.currentTimeMillis()
        )

        val event = BeltEvent(
            id = nextEventId++,
            timestampMillis = System.currentTimeMillis(),
            direction = direction,
            directionConfidence = confidence,
            priority = priority,
            eventConfidence = eventConfidence,
            classificationLabel = label
        )
        _eventHistory.value = (listOf(event) + _eventHistory.value).take(50)
    }

    private fun settleToBackground() {
        val current = _beltState.value
        if (current.state == AwarenessState.EVENT) {
            _beltState.value = current.copy(state = AwarenessState.COOLDOWN)
        }
    }

    override fun handleNotificationEvent(event: CapturedNotification) {
        val label = if (event.isIncomingCall) "INCOMING CALL: ${event.appLabel}" else "PHONE: ${event.appLabel}"
        // .copy(), not a fresh BeltState(...) -- preserves fields this event
        // doesn't touch (lastKeywordMatchName, any already-active call from
        // a different package, etc.) instead of silently resetting them.
        _beltState.value = _beltState.value.copy(
            connected = true,
            direction = Direction.UNKNOWN,
            directionConfidence = 0f,
            eventScore = 1f,
            eventConfidence = 1f, // it's a real notification, not an inferred sound -- classification isn't in doubt
            state = AwarenessState.EVENT,
            priority = event.pattern.priority,
            sameDirectionRepeats = 0,
            trend = Trend.STABLE,
            classificationLabel = label,
            lastEventAtMillis = event.timestampMillis,
            activeCallLabel = if (event.isIncomingCall) label else _beltState.value.activeCallLabel,
            activeCallPackage = if (event.isIncomingCall) event.packageName else _beltState.value.activeCallPackage
        )

        _eventHistory.value = (listOf(
            BeltEvent(
                id = nextEventId++,
                timestampMillis = event.timestampMillis,
                direction = Direction.UNKNOWN,
                directionConfidence = 0f,
                priority = event.pattern.priority,
                eventConfidence = 1f,
                classificationLabel = label
            )
        ) + _eventHistory.value).take(50)
    }

    override fun handleCallEnded(packageName: String) {
        if (_beltState.value.activeCallPackage != packageName) return
        _beltState.value = _beltState.value.copy(activeCallLabel = null, activeCallPackage = null)
    }

    override suspend fun uploadTrainedClass(className: String, centroid: FloatArray): Boolean {
        // Nothing to upload to in mock mode -- logged so the UI flow (record
        // -> train -> sync) is still exercisable without a real belt.
        // Returns true (not a fabricated success -- there is genuinely no
        // write to fail in mock mode, unlike the real repository).
        android.util.Log.i("MockBeltRepository", "uploadTrainedClass('$className', ${centroid.size} features) -- no real belt in mock mode")
        return true
    }

    override suspend fun uploadKeyword(keywordName: String, templates: List<Pair<FloatArray, Int>>): Boolean {
        android.util.Log.i("MockBeltRepository", "uploadKeyword('$keywordName', ${templates.size} templates) -- no real belt in mock mode")
        return true
    }

    override suspend fun deleteKeyword(keywordName: String): Boolean {
        android.util.Log.i("MockBeltRepository", "deleteKeyword('$keywordName') -- no real belt in mock mode")
        return true
    }
}
