package com.hapticbelt.app.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.hapticbelt.app.data.AwarenessState
import com.hapticbelt.app.data.BeltRepository
import com.hapticbelt.app.data.Priority
import kotlinx.coroutines.delay

@Composable
fun EmergencyScreen(repository: BeltRepository) {
    val settings by repository.settings.collectAsState()
    val beltState by repository.beltState.collectAsState()

    var name by remember { mutableStateOf(settings.emergencyContactName) }
    var phone by remember { mutableStateOf(settings.emergencyContactPhone) }

    var showAlert by remember { mutableStateOf(false) }
    var secondsLeft by remember { mutableIntStateOf(settings.acknowledgeTimeoutSeconds) }
    var escalated by remember { mutableStateOf(false) }

    // Real escalation trigger: a CRITICAL priority event that hasn't been
    // acknowledged. This mirrors the original spec's flow exactly:
    // critical event -> haptic warning -> phone alert -> acknowledge? -> escalate.
    LaunchedEffect(beltState.priority, beltState.lastEventAtMillis) {
        if (beltState.priority == Priority.CRITICAL && beltState.state == AwarenessState.EVENT) {
            showAlert = true
            escalated = false
            secondsLeft = settings.acknowledgeTimeoutSeconds
            while (secondsLeft > 0 && showAlert) {
                delay(1000)
                secondsLeft -= 1
            }
            if (showAlert) {
                escalated = true
            }
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
                    Text("Emergency contact", style = MaterialTheme.typography.titleMedium)
                    Text(
                        "Used only for the acknowledge/escalate flow below. This app does NOT " +
                            "automatically determine real danger -- escalation triggers purely on a " +
                            "configured CRITICAL priority event going unacknowledged, per the project's " +
                            "own explicit rule against claiming automatic danger detection.",
                        style = MaterialTheme.typography.bodySmall,
                        color = Color.Gray
                    )
                    OutlinedTextField(
                        value = name,
                        onValueChange = {
                            name = it
                            repository.updateSettings { s -> s.copy(emergencyContactName = it) }
                        },
                        label = { Text("Contact name") },
                        modifier = Modifier.fillMaxWidth()
                    )
                    OutlinedTextField(
                        value = phone,
                        onValueChange = {
                            phone = it
                            repository.updateSettings { s -> s.copy(emergencyContactPhone = it) }
                        },
                        label = { Text("Contact phone") },
                        modifier = Modifier.fillMaxWidth()
                    )
                }
            }
        }

        item {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text("Acknowledge window: ${settings.acknowledgeTimeoutSeconds}s", style = MaterialTheme.typography.titleMedium)
                    Text(
                        "How long the user has to acknowledge a CRITICAL alert before it escalates.",
                        style = MaterialTheme.typography.bodySmall,
                        color = Color.Gray
                    )
                }
            }
        }

        item {
            OutlinedButton(
                onClick = { repository.simulateCriticalEvent() },
                modifier = Modifier.fillMaxWidth()
            ) {
                Text("Test: simulate a CRITICAL event now")
            }
        }

        if (escalated) {
            item {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("ESCALATED (simulated)", color = Color(0xFFEF4444), style = MaterialTheme.typography.titleMedium)
                        Text(
                            "Would now notify ${name.ifBlank { "(no contact set)" }} at ${phone.ifBlank { "(no number set)" }}. " +
                                "Actually sending an SMS/call is not wired up yet -- this is the flow logic only.",
                            style = MaterialTheme.typography.bodySmall
                        )
                    }
                }
            }
        }
    }

    if (showAlert && !escalated) {
        AlertDialog(
            onDismissRequest = { /* must explicitly acknowledge or let it time out */ },
            title = { Text("Critical event detected") },
            text = {
                Text("Direction: ${beltState.direction.name.replace("_", "-")}\nAcknowledge within $secondsLeft s or this will escalate to your emergency contact.")
            },
            confirmButton = {
                Button(onClick = {
                    showAlert = false
                    repository.acknowledgeCurrentEvent()
                }) {
                    Text("Acknowledge")
                }
            }
        )
    }
}
