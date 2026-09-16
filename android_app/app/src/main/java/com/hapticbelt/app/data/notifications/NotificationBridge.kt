package com.hapticbelt.app.data.notifications

import android.content.Context
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Process-wide meeting point between NotificationCaptureService (a system
 * component Android can create independently of MainActivity/MainViewModel)
 * and the rest of the app. Deliberately a plain singleton object rather than
 * something injected: a NotificationListenerService is instantiated by the
 * system, not by us, so there is no constructor we control to hand it a
 * repository reference.
 *
 * The enabled flag is backed by SharedPreferences (not just in-memory State)
 * because the listener service can be running -- and deciding whether to act
 * on notifications -- even when MainActivity/MainViewModel don't currently
 * exist in memory.
 */
object NotificationBridge {
    private const val PREFS_NAME = "notification_bridge_prefs"
    private const val KEY_ENABLED = "enabled"
    private const val LOG_CAPACITY = 50

    private val _capturedEvents = MutableStateFlow<List<CapturedNotification>>(emptyList())
    /** Everything captured so far, most recent first -- the "prove it works" log for the UI. */
    val capturedEvents: StateFlow<List<CapturedNotification>> = _capturedEvents.asStateFlow()

    // Buffered so a relay event isn't lost if MainViewModel's collector isn't
    // subscribed at the exact instant a notification arrives (e.g. briefly
    // during process start).
    private val _relayEvents = MutableSharedFlow<CapturedNotification>(extraBufferCapacity = 16)
    /** Events to actually relay into the active BeltRepository, consumed by MainViewModel. */
    val relayEvents: SharedFlow<CapturedNotification> = _relayEvents

    // Package name of a notification NotificationCaptureService saw removed
    // (answered/declined/ended) -- lets BeltRepository.handleCallEnded()
    // clear activeCallLabel only when it's the SAME call that's ending, not
    // an unrelated notification being dismissed. See NotificationCaptureService.
    private val _callEndedEvents = MutableSharedFlow<String>(extraBufferCapacity = 16)
    val callEndedEvents: SharedFlow<String> = _callEndedEvents

    fun isEnabled(context: Context): Boolean =
        prefs(context).getBoolean(KEY_ENABLED, true)

    fun setEnabled(context: Context, enabled: Boolean) {
        prefs(context).edit().putBoolean(KEY_ENABLED, enabled).apply()
    }

    /** Called by NotificationCaptureService for every notification it decides to keep. */
    fun record(event: CapturedNotification) {
        _capturedEvents.value = (listOf(event) + _capturedEvents.value).take(LOG_CAPACITY)
        _relayEvents.tryEmit(event)
    }

    /** Clears the in-app log only -- does not affect the real notifications still on the phone. */
    fun clear() {
        _capturedEvents.value = emptyList()
    }

    /** Called by NotificationCaptureService when a call notification it previously captured is removed. */
    fun recordCallEnded(packageName: String) {
        _callEndedEvents.tryEmit(packageName)
    }

    private fun prefs(context: Context) =
        context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
}
