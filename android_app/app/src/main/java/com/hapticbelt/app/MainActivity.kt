package com.hapticbelt.app

import android.Manifest
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Dashboard
import androidx.compose.material.icons.filled.Notifications
import androidx.compose.material.icons.filled.NotificationsActive
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.navigation.NavDestination.Companion.hierarchy
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.hapticbelt.app.ui.screens.DashboardScreen
import com.hapticbelt.app.ui.screens.EmergencyScreen
import com.hapticbelt.app.ui.screens.LiveAwarenessScreen
import com.hapticbelt.app.ui.screens.KeywordEnrollScreen
import com.hapticbelt.app.ui.screens.NotificationBridgeScreen
import com.hapticbelt.app.ui.screens.SettingsScreen
import com.hapticbelt.app.ui.screens.SoundTrainingScreen
import com.hapticbelt.app.ui.theme.HapticBeltTheme

private enum class Tab(val route: String, val label: String) {
    Dashboard("dashboard", "Dashboard"),
    Live("live", "Live"),
    Notify("notify", "Notify"),
    Settings("settings", "Settings"),
    Emergency("emergency", "Emergency")
}

class MainActivity : ComponentActivity() {
    private val viewModel: MainViewModel by viewModels()

    // BleBeltRepository (used now that USE_BLE_REPOSITORY = true) checks
    // these permissions before every scan/connect attempt but never
    // triggers the system prompt itself -- without this, a fresh install
    // would silently never scan (just a logcat warning) rather than
    // actually connecting. Its connection loop polls every 5s, so whatever
    // gets granted here is picked up on its own without needing a restart.
    private val blePermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { /* BleBeltRepository's own poll loop reacts on its next tick */ }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        requestBlePermissionsIfNeeded()
        setContent {
            HapticBeltTheme {
                AppRoot(viewModel)
            }
        }
    }

    private fun requestBlePermissionsIfNeeded() {
        val permissions = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        blePermissionLauncher.launch(permissions)
    }
}

@Composable
private fun AppRoot(viewModel: MainViewModel) {
    val navController = rememberNavController()

    Scaffold(
        bottomBar = {
            NavigationBar {
                val backStackEntry by navController.currentBackStackEntryAsState()
                val currentDestination = backStackEntry?.destination

                Tab.entries.forEach { tab ->
                    val selected = currentDestination?.hierarchy?.any { it.route == tab.route } == true
                    NavigationBarItem(
                        selected = selected,
                        onClick = {
                            navController.navigate(tab.route) {
                                popUpTo(navController.graph.findStartDestination().id) { saveState = true }
                                launchSingleTop = true
                                restoreState = true
                            }
                        },
                        icon = { Icon(iconFor(tab), contentDescription = tab.label) },
                        label = { Text(tab.label) }
                    )
                }
            }
        }
    ) { padding ->
        NavHost(
            navController = navController,
            startDestination = Tab.Dashboard.route,
            modifier = Modifier.padding(padding)
        ) {
            composable(Tab.Dashboard.route) { DashboardScreen(viewModel.repository) }
            composable(Tab.Live.route) { LiveAwarenessScreen(viewModel.repository) }
            composable(Tab.Notify.route) { NotificationBridgeScreen() }
            composable(Tab.Settings.route) {
                SettingsScreen(
                    repository = viewModel.repository,
                    onNavigateToSoundTraining = { navController.navigate("sound_training") },
                    onNavigateToKeywordEnroll = { navController.navigate("keyword_enroll") }
                )
            }
            composable(Tab.Emergency.route) { EmergencyScreen(viewModel.repository) }
            // Reached from Settings, not a bottom-nav tab -- the nav bar is
            // already tight on a phone-width screen (5 tabs, labels already
            // wrap), so these stay pushed screens instead of more tabs.
            composable("sound_training") { SoundTrainingScreen(viewModel.repository) }
            composable("keyword_enroll") { KeywordEnrollScreen(viewModel.repository) }
        }
    }
}

private fun iconFor(tab: Tab) = when (tab) {
    Tab.Dashboard -> Icons.Filled.Dashboard
    Tab.Live -> Icons.Filled.Notifications
    Tab.Notify -> Icons.Filled.NotificationsActive
    Tab.Settings -> Icons.Filled.Settings
    Tab.Emergency -> Icons.Filled.Warning
}
