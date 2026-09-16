package com.hapticbelt.app.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

val AccentBlue = Color(0xFF3B82F6)
val AccentGreen = Color(0xFF22C55E)
val AccentGold = Color(0xFFEAB308)
val AccentOrange = Color(0xFFF97316)
val AccentRed = Color(0xFFEF4444)
val AccentPurple = Color(0xFF8B5CF6)

private val DarkColors = darkColorScheme(
    primary = AccentBlue,
    secondary = AccentGreen,
    error = AccentRed
)

private val LightColors = lightColorScheme(
    primary = AccentBlue,
    secondary = AccentGreen,
    error = AccentRed
)

@Composable
fun HapticBeltTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit
) {
    val colors = if (darkTheme) DarkColors else LightColors
    MaterialTheme(colorScheme = colors, content = content)
}

// State/priority -> color mapping shared across screens, so the same
// semantic always renders the same color everywhere in the app.
fun colorForState(state: com.hapticbelt.app.data.AwarenessState): Color = when (state) {
    com.hapticbelt.app.data.AwarenessState.BACKGROUND -> AccentGreen
    com.hapticbelt.app.data.AwarenessState.NOTICE -> AccentGold
    com.hapticbelt.app.data.AwarenessState.CANDIDATE -> AccentOrange
    com.hapticbelt.app.data.AwarenessState.EVENT -> AccentRed
    com.hapticbelt.app.data.AwarenessState.COOLDOWN -> AccentPurple
}

fun colorForPriority(priority: com.hapticbelt.app.data.Priority): Color = when (priority) {
    com.hapticbelt.app.data.Priority.LOW -> Color.Gray
    com.hapticbelt.app.data.Priority.MEDIUM -> AccentGold
    com.hapticbelt.app.data.Priority.HIGH -> AccentOrange
    com.hapticbelt.app.data.Priority.CRITICAL -> AccentRed
}
