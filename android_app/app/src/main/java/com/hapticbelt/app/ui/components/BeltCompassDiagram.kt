package com.hapticbelt.app.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.hapticbelt.app.data.Direction

// Same 4-point layout as tools/compass_view.py: FRONT top, RIGHT right,
// BACK bottom, LEFT left, around a center "belt" point.
private data class Node(val label: String, val active: Boolean, val dx: Float, val dy: Float)

@Composable
fun BeltCompassDiagram(
    direction: Direction,
    directionConfidence: Float,
    modifier: Modifier = Modifier
) {
    val activeSet = directionToActiveMics(direction)

    val nodes = listOf(
        Node("FRONT", "FRONT" in activeSet, 0f, -1f),
        Node("RIGHT", "RIGHT" in activeSet, 1f, 0f),
        Node("BACK", "BACK" in activeSet, 0f, 1f),
        Node("LEFT", "LEFT" in activeSet, -1f, 0f),
    )
    val isGlobal = direction == Direction.OMNIDIRECTIONAL
    val isUnknown = direction == Direction.UNKNOWN

    Box(modifier = modifier.fillMaxWidth().aspectRatio(1f), contentAlignment = Alignment.Center) {
        Canvas(modifier = Modifier.fillMaxWidth().aspectRatio(1f)) {
            val center = Offset(size.width / 2f, size.height / 2f)
            val radius = size.minDimension * 0.32f

            // spokes
            nodes.forEach { n ->
                drawLine(
                    color = Color.LightGray,
                    start = center,
                    end = Offset(center.x + n.dx * radius, center.y + n.dy * radius),
                    strokeWidth = 2f
                )
            }

            // center "belt" dot
            drawCircle(color = Color.DarkGray, radius = size.minDimension * 0.06f, center = center)

            // direction nodes
            nodes.forEach { n ->
                val pos = Offset(center.x + n.dx * radius, center.y + n.dy * radius)
                val nodeColor = when {
                    isUnknown -> Color.LightGray
                    isGlobal -> Color(0xFFF97316)
                    n.active -> Color(0xFFEF4444)
                    else -> Color(0xFF3B82F6)
                }
                val alpha = if (n.active || isGlobal) 0.35f + 0.55f * directionConfidence.coerceIn(0f, 1f) else 0.25f
                drawCircle(
                    color = nodeColor.copy(alpha = alpha),
                    radius = size.minDimension * 0.13f,
                    center = pos
                )
                drawCircle(
                    color = nodeColor,
                    radius = size.minDimension * 0.13f,
                    center = pos,
                    style = Stroke(width = 3f)
                )
            }
        }

        // labels, positioned via a second overlay for simplicity
        nodes.forEach { n ->
            val offsetLabel = when (n.label) {
                "FRONT" -> Alignment.TopCenter
                "RIGHT" -> Alignment.CenterEnd
                "BACK" -> Alignment.BottomCenter
                else -> Alignment.CenterStart
            }
            Box(modifier = Modifier.fillMaxWidth().aspectRatio(1f), contentAlignment = offsetLabel) {
                Text(
                    text = n.label,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onBackground
                )
            }
        }
    }
}

// Mirrors spatial_detector.cpp's ledsForDirection()/mergedDirection() logic
// exactly, so the phone's visual matches what the belt's own LEDs would show.
private fun directionToActiveMics(direction: Direction): Set<String> = when (direction) {
    Direction.FRONT -> setOf("FRONT")
    Direction.RIGHT -> setOf("RIGHT")
    Direction.BACK -> setOf("BACK")
    Direction.LEFT -> setOf("LEFT")
    Direction.FRONT_RIGHT -> setOf("FRONT", "RIGHT")
    Direction.RIGHT_BACK -> setOf("RIGHT", "BACK")
    Direction.BACK_LEFT -> setOf("BACK", "LEFT")
    Direction.LEFT_FRONT -> setOf("LEFT", "FRONT")
    Direction.OMNIDIRECTIONAL -> setOf("FRONT", "RIGHT", "BACK", "LEFT")
    Direction.UNKNOWN -> emptySet()
}
