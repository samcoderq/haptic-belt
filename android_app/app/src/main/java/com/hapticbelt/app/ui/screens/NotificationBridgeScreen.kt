package com.hapticbelt.app.ui.screens

import android.content.Intent
import android.provider.Settings
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.foundation.rememberScrollState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.app.NotificationManagerCompat
import androidx.lifecycle.compose.LifecycleEventEffect
import com.hapticbelt.app.data.Priority
import com.hapticbelt.app.data.notifications.AppPatternPreferences
import com.hapticbelt.app.data.notifications.CapturedNotification
import com.hapticbelt.app.data.notifications.NotificationBridge
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Shows the real, live output of NotificationCaptureService -- not a mock.
 * Nothing on this screen is simulated: the permission check reflects actual
 * system state, and the log is exactly what NotificationBridge.record() has
 * received from real notifications posted on this phone.
 */
@Composable
fun NotificationBridgeScreen() {
    val context = LocalContext.current
    val events by NotificationBridge.capturedEvents.collectAsState()

    // Local mirror of AppPatternPreferences (SharedPreferences, not a Flow) --
    // updated alongside every write so the UI reflects a change immediately
    // without needing to re-read prefs on every recomposition.
    var overrides by remember { mutableStateOf(AppPatternPreferences.getAllOverrides(context)) }

    var accessGranted by remember { mutableStateOf(isListenerAccessGranted(context)) }
    // Re-check on every resume: the grant happens in a separate system
    // settings screen, so this screen needs to notice the change on return.
    LifecycleEventEffect(androidx.lifecycle.Lifecycle.Event.ON_RESUME) {
        accessGranted = isListenerAccessGranted(context)
    }

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        item {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text("Notification access", style = MaterialTheme.typography.titleMedium)
                    Text(
                        if (accessGranted) {
                            "Granted -- real phone notifications are being captured below."
                        } else {
                            "Not granted yet. Android requires this to be turned on from system settings, not from an in-app dialog."
                        },
                        style = MaterialTheme.typography.bodySmall,
                        color = if (accessGranted) Color(0xFF2E7D32) else Color.Gray
                    )
                    if (!accessGranted) {
                        Button(onClick = {
                            context.startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS))
                        }) {
                            Text("Open notification access settings")
                        }
                    }
                }
            }
        }

        // Plain (non-remembered) computation -- this lambda is LazyListScope,
        // not a @Composable scope, so `remember` isn't callable here; a
        // distinct() over a capped 50-item list is cheap enough to redo per
        // recomposition anyway.
        val knownApps = events.map { it.packageName to it.appLabel }.distinct()
        if (knownApps.isNotEmpty()) {
            item {
                Text(
                    "Custom patterns per app",
                    style = MaterialTheme.typography.titleMedium,
                    modifier = Modifier.padding(top = 4.dp)
                )
            }
            item {
                Text(
                    "Pin a fixed pattern for a specific app instead of the automatic category-based one -- only apps that have sent a captured notification show up here.",
                    style = MaterialTheme.typography.bodySmall,
                    color = Color.Gray
                )
            }
            items(knownApps, key = { it.first }) { (packageName, appLabel) ->
                AppPatternRow(
                    appLabel = appLabel,
                    current = overrides[packageName],
                    onSelect = { priority ->
                        if (priority == null) {
                            AppPatternPreferences.clearOverride(context, packageName)
                            overrides = overrides - packageName
                        } else {
                            AppPatternPreferences.setOverride(context, packageName, priority)
                            overrides = overrides + (packageName to priority)
                        }
                    }
                )
            }
        }

        item {
            Row(
                modifier = Modifier.fillMaxWidth().padding(top = 4.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text("Captured notifications (${events.size})", style = MaterialTheme.typography.titleMedium)
                if (events.isNotEmpty()) {
                    TextButton(onClick = { NotificationBridge.clear() }) {
                        Text("Clear")
                    }
                }
            }
        }

        if (events.isEmpty()) {
            item {
                Text(
                    "Nothing captured yet. Trigger a notification (message, call, alarm...) on this phone to see it mapped to a haptic pattern here.",
                    style = MaterialTheme.typography.bodySmall,
                    color = Color.Gray
                )
            }
        }

        items(events, key = { it.id }) { event -> CapturedNotificationRow(event) }
    }
}

@Composable
private fun AppPatternRow(
    appLabel: String,
    current: Priority?,
    onSelect: (Priority?) -> Unit
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(appLabel, style = MaterialTheme.typography.bodyLarge)
            Row(
                modifier = Modifier.horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                FilterChip(
                    selected = current == null,
                    onClick = { onSelect(null) },
                    label = { Text("Auto") }
                )
                Priority.entries.forEach { priority ->
                    FilterChip(
                        selected = current == priority,
                        onClick = { onSelect(priority) },
                        label = { Text(priority.name) }
                    )
                }
            }
        }
    }
}

@Composable
private fun CapturedNotificationRow(event: CapturedNotification) {
    val timeFormat = remember { SimpleDateFormat("HH:mm:ss", Locale.getDefault()) }
    Card(modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(12.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(event.appLabel, style = MaterialTheme.typography.bodyLarge)
                Text(
                    event.title ?: "(no title)",
                    style = MaterialTheme.typography.bodySmall,
                    color = Color.Gray
                )
                Text(
                    "${timeFormat.format(Date(event.timestampMillis))} -- category: ${event.category ?: "none"}",
                    style = MaterialTheme.typography.bodySmall,
                    color = Color.Gray
                )
            }
            Column(horizontalAlignment = Alignment.End) {
                Text(event.pattern.priority.name, style = MaterialTheme.typography.labelLarge)
                Text(event.pattern.patternName, style = MaterialTheme.typography.bodySmall, color = Color.Gray)
            }
        }
    }
}

private fun isListenerAccessGranted(context: android.content.Context): Boolean =
    NotificationManagerCompat.getEnabledListenerPackages(context).contains(context.packageName)
