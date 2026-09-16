package com.hapticbelt.app.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Card
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.hapticbelt.app.data.BeltEvent
import com.hapticbelt.app.data.BeltRepository
import com.hapticbelt.app.ui.components.BeltCompassDiagram
import com.hapticbelt.app.ui.theme.colorForPriority
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@Composable
fun LiveAwarenessScreen(repository: BeltRepository) {
    val state by repository.beltState.collectAsState()
    val history by repository.eventHistory.collectAsState()

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        item {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    BeltCompassDiagram(
                        direction = state.direction,
                        directionConfidence = state.directionConfidence
                    )
                }
            }
        }

        item {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text("EVENT", style = MaterialTheme.typography.labelLarge, color = Color.Gray)
                    Text(state.classificationLabel, style = MaterialTheme.typography.headlineSmall)
                    Row(horizontalArrangement = Arrangement.spacedBy(24.dp)) {
                        Column {
                            Text("DIRECTION", style = MaterialTheme.typography.labelSmall, color = Color.Gray)
                            Text(state.direction.name.replace("_", "-"), fontWeight = FontWeight.Bold)
                        }
                        Column {
                            Text("CONFIDENCE", style = MaterialTheme.typography.labelSmall, color = Color.Gray)
                            Text("%.0f%%".format(state.directionConfidence * 100), fontWeight = FontWeight.Bold)
                        }
                        Column {
                            Text("PRIORITY", style = MaterialTheme.typography.labelSmall, color = Color.Gray)
                            Text(
                                state.priority.name,
                                fontWeight = FontWeight.Bold,
                                color = colorForPriority(state.priority)
                            )
                        }
                    }
                }
            }
        }

        item {
            Text("Recent activity", style = MaterialTheme.typography.titleMedium)
        }

        if (history.isEmpty()) {
            item {
                Text("Nothing yet -- waiting for the first simulated event.", color = Color.Gray)
            }
        }

        items(history, key = { it.id }) { event ->
            EventRow(event)
            HorizontalDivider()
        }
    }
}

@Composable
private fun EventRow(event: BeltEvent) {
    val timeFmt = remember { SimpleDateFormat("HH:mm:ss", Locale.getDefault()) }
    Row(
        modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Column {
            Text("${event.direction.name.replace("_", "-")}  --  ${event.classificationLabel}", fontWeight = FontWeight.SemiBold)
            Text(timeFmt.format(Date(event.timestampMillis)), color = Color.Gray, style = MaterialTheme.typography.bodySmall)
        }
        Text(event.priority.name, color = colorForPriority(event.priority), fontWeight = FontWeight.Bold)
    }
}
