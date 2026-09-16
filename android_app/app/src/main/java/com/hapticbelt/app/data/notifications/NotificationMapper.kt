package com.hapticbelt.app.data.notifications

import android.app.Notification
import android.service.notification.StatusBarNotification
import com.hapticbelt.app.data.Priority

// Pure classification logic, deliberately kept separate from
// NotificationCaptureService so it can be reasoned about (and unit tested)
// without needing a live NotificationListenerService connection -- that
// service can only be exercised on-device with notification access granted,
// this function can be exercised on any StatusBarNotification-shaped input.
object NotificationMapper {

    // Notification.category values (Android's own constants) that a deaf user
    // would reasonably want escalated above a normal app notification.
    // CATEGORY_CALL alone is NOT enough to mean "ringing" -- see callType below.
    private val HIGH_CATEGORIES = setOf(Notification.CATEGORY_MESSAGE, Notification.CATEGORY_EVENT, Notification.CATEGORY_REMINDER)

    // Notification.CallStyle's call-type extra (android.app.Notification, API 31+).
    // Hardcoded rather than referenced from Notification.CallStyle directly since
    // that class requires a higher minSdk than this app targets (26) -- a dialer
    // built before CallStyle existed simply won't set this extra at all, and is
    // handled by the "callType unknown" fallback below.
    private const val EXTRA_CALL_TYPE = "android.callType" // Notification.EXTRA_CALL_TYPE
    private const val CALL_TYPE_INCOMING = 1 // Notification.CallStyle.CALL_TYPE_INCOMING
    private const val CALL_TYPE_ONGOING = 2   // Notification.CallStyle.CALL_TYPE_ONGOING
    private const val CALL_TYPE_SCREENING = 3 // Notification.CallStyle.CALL_TYPE_SCREENING
    // CATEGORY_CALL covers the whole call lifecycle (ringing, answered, outgoing),
    // not just an incoming ring -- confirmed on-device: calling out shows the same
    // category=call with callType=2 (ONGOING).
    //
    // v2 (2026-09-16): flipped from "assume NOT incoming unless callType proves
    // it" to "assume incoming unless callType proves otherwise" -- real-device
    // test found WhatsApp's call notification sets category=call but never
    // sets the CallStyle callType extra at all (that API is Android 12+ and
    // plenty of VOIP apps, WhatsApp included, don't use it), so callType came
    // back -1 (unknown) and the original conservative check silently dropped
    // the notification before it ever reached the app -- worse than the
    // false-positive risk (an already-answered/outgoing call briefly showing
    // as "incoming") it was written to avoid. Only a callType that explicitly
    // says ONGOING or SCREENING is now treated as not-incoming.

    /**
     * Returns null for notifications this bridge should ignore entirely --
     * ongoing/foreground-service notifications (media transport controls,
     * download progress, this app's own belt-connection notice) and group
     * summaries (a duplicate of a notification already seen individually).
     *
     * [overridePriority] comes from AppPatternPreferences (a user-chosen
     * fixed priority for this package) and, when set, wins over the
     * category-based default entirely -- including which priority counts as
     * CRITICAL for the ongoing/foreground-service filter below. Dialer and
     * clock apps commonly mark their important notifications
     * ongoing/foreground -- non-dismissible specifically because they're
     * important -- so applying that filter to a priority the user (or the
     * call/alarm category) has marked CRITICAL would silently drop it.
     */
    fun classify(sbn: StatusBarNotification, appLabel: String, overridePriority: Priority? = null): CapturedNotification? {
        val notification = sbn.notification ?: return null
        if (notification.flags and Notification.FLAG_GROUP_SUMMARY != 0) return null

        val category = notification.category
        val callType = notification.extras?.getInt(EXTRA_CALL_TYPE, -1) ?: -1
        // A call only counts as CRITICAL when we can positively confirm it's an
        // incoming ring. If callType is missing (pre-CallStyle dialer), fall back
        // to MEDIUM rather than assume incoming -- a false "everything is urgent"
        // erodes trust in what CRITICAL means faster than a missed edge case does.
        val isRingingCall = category == Notification.CATEGORY_CALL &&
            callType != CALL_TYPE_ONGOING && callType != CALL_TYPE_SCREENING
        val categoryIsCritical = isRingingCall || category == Notification.CATEGORY_ALARM

        val priority = overridePriority ?: when {
            categoryIsCritical -> Priority.CRITICAL
            category in HIGH_CATEGORIES -> Priority.HIGH
            else -> Priority.MEDIUM // most app notifications (social, email, promos) land here
        }
        val isCritical = priority == Priority.CRITICAL

        if (!isCritical) {
            if (notification.flags and Notification.FLAG_ONGOING_EVENT != 0) return null
            if (notification.flags and Notification.FLAG_FOREGROUND_SERVICE != 0) return null
        }

        val pattern = patternFor(priority)

        val extras = notification.extras
        val title = extras?.getCharSequence(Notification.EXTRA_TITLE)?.toString()

        return CapturedNotification(
            id = sbn.postTime,
            timestampMillis = sbn.postTime,
            appLabel = appLabel,
            packageName = sbn.packageName,
            category = category,
            title = title,
            pattern = pattern,
            isIncomingCall = isRingingCall
        )
    }

    private fun patternFor(priority: Priority): HapticPattern = when (priority) {
        Priority.CRITICAL -> HapticPattern(priority, pulseCount = 4, patternName = "sustained-buzz")
        Priority.HIGH -> HapticPattern(priority, pulseCount = 3, patternName = "triple-pulse")
        Priority.MEDIUM -> HapticPattern(priority, pulseCount = 2, patternName = "double-pulse")
        Priority.LOW -> HapticPattern(priority, pulseCount = 1, patternName = "single-pulse")
    }
}
