package com.hapticbelt.app.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.foundation.background
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.runtime.collectAsState
import com.hapticbelt.app.data.AwarenessState
import com.hapticbelt.app.data.BeltRepository
import com.hapticbelt.app.ui.theme.colorForPriority
import com.hapticbelt.app.ui.theme.colorForState

@Composable
fun DashboardScreen(repository: BeltRepository) {
    val state by repository.beltState.collectAsState()

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        item {
            Card(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth().padding(16.dp),
                    verticalAlignment = androidx.compose.ui.Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Column {
                        Text("Belt Status", style = MaterialTheme.typography.titleMedium)
                        Text(
                            if (state.connected) "Connected" else "Disconnected",
                            color = if (state.connected) Color(0xFF22C55E) else Color.Gray
                        )
                    }
                    ConnectionDot(connected = state.connected)
                }
            }
        }

        item {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Text("Current Event", style = MaterialTheme.typography.titleMedium)
                    LabeledRow("State", state.state.name, colorForState(state.state))
                    LabeledRow("Direction", state.direction.name.replace("_", "-"))
                    LabeledRow("Direction confidence", "%.0f%%".format(state.directionConfidence * 100))
                    LabeledRow("Priority", state.priority.name, colorForPriority(state.priority))
                    LabeledRow("Classification", state.classificationLabel)
                }
            }
        }

        item {
            val lastMatchAt = state.lastKeywordMatchAtMillis
            val secondsAgo = lastMatchAt?.let { (System.currentTimeMillis() - it) / 1000 }
            val isFresh = secondsAgo != null && secondsAgo < 5
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Text("Keyword", style = MaterialTheme.typography.titleMedium)
                    if (state.lastKeywordMatchName == null) {
                        Text(
                            "No keyword detected yet.",
                            color = Color.Gray
                        )
                    } else {
                        LabeledRow(
                            "Last detected",
                            state.lastKeywordMatchName!!,
                            valueColor = if (isFresh) Color(0xFF22C55E) else Color.Unspecified
                        )
                        LabeledRow(
                            "When",
                            when {
                                secondsAgo == null -> "-"
                                secondsAgo < 1 -> "just now"
                                secondsAgo < 60 -> "${secondsAgo}s ago"
                                else -> "${secondsAgo / 60}m ago"
                            }
                        )
                    }
                    Text(
                        "One-shot pulse from the belt (BLE State packet byte 12) matched against this " +
                            "phone's local enrollment record -- not a read-back from the belt itself, and " +
                            "matching quality against real speech is unproven (see KeywordEnrollScreen).",
                        style = MaterialTheme.typography.bodySmall,
                        color = Color.Gray
                    )
                }
            }
        }

        item {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Text("System", style = MaterialTheme.typography.titleMedium)
                    LabeledRow("Same-direction repeats", state.sameDirectionRepeats.toString())
                    LabeledRow("Trend", state.trend.name)
                    Text(
                        "Note: heart rate / battery will appear here once MAX30102 and real BLE are wired in.",
                        style = MaterialTheme.typography.bodySmall,
                        color = Color.Gray
                    )
                }
            }
        }
    }
}

@Composable
private fun ConnectionDot(connected: Boolean) {
    Row(
        modifier = Modifier
            .size(14.dp)
            .background(if (connected) Color(0xFF22C55E) else Color(0xFFEF4444), CircleShape)
    ) {}
}

@Composable
fun LabeledRow(label: String, value: String, valueColor: Color = Color.Unspecified) {
    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, color = Color.Gray)
        Text(value, fontWeight = FontWeight.SemiBold, color = valueColor)
    }
}
