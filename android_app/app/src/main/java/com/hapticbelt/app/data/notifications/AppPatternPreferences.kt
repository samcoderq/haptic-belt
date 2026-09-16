package com.hapticbelt.app.data.notifications

import android.content.Context
import com.hapticbelt.app.data.Priority

/**
 * Per-app haptic pattern overrides -- lets the user pin a specific package
 * (e.g. "always CRITICAL for WhatsApp calls" or "always LOW for a noisy
 * social app") to a fixed priority instead of relying on NotificationMapper's
 * category-based default. SharedPreferences-backed for the same reason as
 * NotificationBridge's enabled flag: NotificationCaptureService reads this on
 * every notification and can run independent of the app's UI being open.
 */
object AppPatternPreferences {
    private const val PREFS_NAME = "app_pattern_prefs"

    fun getOverride(context: Context, packageName: String): Priority? {
        val stored = prefs(context).getString(packageName, null) ?: return null
        return try {
            Priority.valueOf(stored)
        } catch (e: IllegalArgumentException) {
            null // stored value doesn't match a current Priority name -- treat as unset
        }
    }

    fun setOverride(context: Context, packageName: String, priority: Priority) {
        prefs(context).edit().putString(packageName, priority.name).apply()
    }

    /** Removes the override so this package falls back to category-based classification. */
    fun clearOverride(context: Context, packageName: String) {
        prefs(context).edit().remove(packageName).apply()
    }

    /** All current overrides, keyed by package name -- for the settings UI. */
    fun getAllOverrides(context: Context): Map<String, Priority> {
        return prefs(context).all.mapNotNull { (packageName, value) ->
            val priority = (value as? String)?.let {
                try { Priority.valueOf(it) } catch (e: IllegalArgumentException) { null }
            }
            priority?.let { packageName to it }
        }.toMap()
    }

    private fun prefs(context: Context) =
        context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
}
