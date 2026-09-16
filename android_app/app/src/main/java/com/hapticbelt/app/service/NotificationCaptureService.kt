package com.hapticbelt.app.service

import android.content.pm.PackageManager
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Log
import com.hapticbelt.app.data.notifications.AppPatternPreferences
import com.hapticbelt.app.data.notifications.NotificationBridge
import com.hapticbelt.app.data.notifications.NotificationMapper

/**
 * Real Android NotificationListenerService -- this is the actual capture
 * mechanism, not a simulation of one. Requires the user to grant Notification
 * Access in system settings (a special permission that can't be requested
 * via the normal runtime-permission dialog); see NotificationBridgeScreen for
 * the in-app button that opens that settings page.
 *
 * What this proves end to end right now: a real notification posted anywhere
 * on the phone reaches this service, gets classified by NotificationMapper,
 * and shows up in NotificationBridge.capturedEvents / gets relayed into the
 * active BeltRepository's state (see MainViewModel). What it does NOT do yet:
 * actually vibrate a belt -- there are no motors and no firmware BLE server
 * to relay to. That last hop is a hardware dependency, not a missing feature
 * here.
 */
class NotificationCaptureService : NotificationListenerService() {

    override fun onListenerConnected() {
        super.onListenerConnected()
        Log.i(TAG, "Notification listener connected")
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        if (sbn.packageName == packageName) return // ignore this app's own notifications
        if (!NotificationBridge.isEnabled(applicationContext)) return

        val appLabel = labelFor(sbn.packageName)
        val override = AppPatternPreferences.getOverride(applicationContext, sbn.packageName)
        val captured = NotificationMapper.classify(sbn, appLabel, override) ?: return
        NotificationBridge.record(captured)
    }

    private fun labelFor(packageName: String): String {
        return try {
            val pm = packageManager
            val appInfo = pm.getApplicationInfo(packageName, PackageManager.GET_META_DATA)
            pm.getApplicationLabel(appInfo).toString()
        } catch (e: PackageManager.NameNotFoundException) {
            packageName
        }
    }

    companion object {
        private const val TAG = "NotificationCapture"
    }
}
