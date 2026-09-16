package com.hapticbelt.app.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.hapticbelt.app.data.BeltRepository
import com.hapticbelt.app.data.OperatingMode
import com.hapticbelt.app.data.notifications.NotificationBridge

@Composable
fun SettingsScreen(
    repository: BeltRepository,
    onNavigateToSoundTraining: () -> Unit = {},
    onNavigateToKeywordEnroll: () -> Unit = {}
) {
    val settings by repository.settings.collectAsState()
    val context = LocalContext.current

    // NotificationBridge persists the enabled flag across process restarts
    // (it has to -- the listener service can run without this screen ever
    // having been opened); repository.settings always starts from the
    // BeltSettings() default instead. Reconcile once so the switch shown
    // here matches what the service is actually doing.
    androidx.compose.runtime.LaunchedEffect(Unit) {
        val persisted = NotificationBridge.isEnabled(context)
        if (persisted != settings.notificationBridgeEnabled) {
            repository.updateSettings { it.copy(notificationBridgeEnabled = persisted) }
        }
    }

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        item {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text("Operating mode", style = MaterialTheme.typography.titleMedium)
                    Text(
                        "Different environments need different sensitivity -- research from the project's own review flagged this as important, not yet enforced on the firmware side.",
                        style = MaterialTheme.typography.bodySmall,
                        color = Color.Gray
                    )
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        OperatingMode.entries.forEach { mode ->
                            FilterChip(
                                selected = settings.operatingMode == mode,
                                onClick = { repository.updateSettings { it.copy(operatingMode = mode) } },
                                label = { Text(mode.name) }
                            )
                        }
                    }
                }
            }
        }

        item {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text("Vibration intensity", style = MaterialTheme.typography.titleMedium)
                    Text(
                        "Applies once real vibration motors replace the LED test rig.",
                        style = MaterialTheme.typography.bodySmall,
                        color = Color.Gray
                    )
                    Slider(
                        value = settings.vibrationIntensity,
                        onValueChange = { v -> repository.updateSettings { it.copy(vibrationIntensity = v) } }
                    )
                }
            }
        }

        item {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text("Sensitivity", style = MaterialTheme.typography.titleMedium)
                    Text(
                        "Higher = catches quieter/farther sounds but more false positives (the tradeoff this project has been tuning all along).",
                        style = MaterialTheme.typography.bodySmall,
                        color = Color.Gray
                    )
                    Slider(
                        value = settings.sensitivity,
                        onValueChange = { v -> repository.updateSettings { it.copy(sensitivity = v) } }
                    )
                }
            }
        }

        item {
            Card(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth().padding(16.dp),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Column {
                        Text("Phone notification bridge", style = MaterialTheme.typography.titleMedium)
                        Text("Convert phone notifications into belt haptic patterns -- see the Notify tab to grant access and watch it capture real notifications", style = MaterialTheme.typography.bodySmall, color = Color.Gray)
                    }
                    Switch(
                        checked = settings.notificationBridgeEnabled,
                        onCheckedChange = { v ->
                            repository.updateSettings { it.copy(notificationBridgeEnabled = v) }
                            NotificationBridge.setEnabled(context, v)
                        }
                    )
                }
            }
        }

        item {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text("Sound training", style = MaterialTheme.typography.titleMedium)
                    Text(
                        "Record labeled samples of important sounds and sync them straight to the belt over Bluetooth -- no laptop, no reflash.",
                        style = MaterialTheme.typography.bodySmall,
                        color = Color.Gray
                    )
                    Button(onClick = onNavigateToSoundTraining) {
                        Text("Open sound training")
                    }
                }
            }
        }

        item {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text("Keyword / name detection", style = MaterialTheme.typography.titleMedium)
                    Text(
                        "Say a word or name 5 times to enroll it -- a different technique from sound training, matches spoken patterns instead of a sound's timbre.",
                        style = MaterialTheme.typography.bodySmall,
                        color = Color.Gray
                    )
                    Button(onClick = onNavigateToKeywordEnroll) {
                        Text("Open keyword enrollment")
                    }
                }
            }
        }
    }
}
