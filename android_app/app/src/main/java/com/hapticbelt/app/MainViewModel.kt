package com.hapticbelt.app

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.hapticbelt.app.data.BeltRepository
import com.hapticbelt.app.data.MockBeltRepository
import com.hapticbelt.app.data.ble.BleBeltRepository
import com.hapticbelt.app.data.notifications.NotificationBridge
import kotlinx.coroutines.launch

// Build-time switch between the fully-simulated demo repository and the
// real BLE repository. Now true: real firmware advertising the service in
// android_app/BLE_PROTOCOL.md exists and is flashed to the belt, so this is
// the real (final) path rather than the simulated demo. Flip back to false
// to fall back to MockBeltRepository (e.g. no belt powered on / in range).
// AndroidViewModel (not plain ViewModel) is needed here solely to get an
// application Context for BleBeltRepository's BluetoothManager lookup.
private const val USE_BLE_REPOSITORY = true

// One repository instance for the whole app -- every screen already talks
// to the BeltRepository interface, not to a concrete implementation.
class MainViewModel(application: Application) : AndroidViewModel(application) {
    val repository: BeltRepository =
        if (USE_BLE_REPOSITORY) BleBeltRepository(application) else MockBeltRepository()

    init {
        viewModelScope.launch {
            repository.start()
        }
        // NotificationCaptureService can't reach a repository directly (it's
        // instantiated by the system, not by us), so it publishes here and
        // this is where captured notifications actually reach the app's
        // visible state -- gated on the same settings toggle the Settings
        // screen shows, checked fresh per event rather than at collection
        // start so flipping the switch takes effect immediately.
        viewModelScope.launch {
            NotificationBridge.relayEvents.collect { event ->
                if (repository.settings.value.notificationBridgeEnabled) {
                    repository.handleNotificationEvent(event)
                }
            }
        }
        // Clears BeltState.activeCallLabel when the call that set it actually
        // ends -- see NotificationCaptureService.onNotificationRemoved and
        // BeltModels.kt's activeCallLabel doc comment for why this needs its
        // own signal separate from relayEvents above.
        viewModelScope.launch {
            NotificationBridge.callEndedEvents.collect { packageName ->
                repository.handleCallEnded(packageName)
            }
        }
    }

    override fun onCleared() {
        repository.stop()
        super.onCleared()
    }
}
