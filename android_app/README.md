# Haptic Belt Companion App

Kotlin + Jetpack Compose app for the Haptic Awareness Belt. Currently runs entirely on
**simulated data** (`MockBeltRepository`) so it's fully interactive without the belt connected --
built during a stretch with no hardware access. Swapping in real BLE later means writing one new
`BleBeltRepository` class implementing the same `BeltRepository` interface; no screen code changes.

## First-time setup (once Android Studio finishes installing)

1. Open Android Studio -> **Open** -> select this `android_app` folder.
2. Let it sync -- `gradlew`/`gradlew.bat` and `gradle/wrapper/gradle-wrapper.jar` are committed now
   (Gradle 8.7), so this should just work without Android Studio needing to repair anything.
3. If prompted to install a missing SDK platform (API 34) or build tools, accept -- Android
   Studio's SDK Manager handles this.

Command-line build (needs JDK 17 -- Gradle 8.7 does not run on newer JDKs like 21+):
`JAVA_HOME=/path/to/jdk-17 ./gradlew assembleDebug` (or `gradlew.bat` on Windows). Verified working
in this repo with Eclipse Temurin 17.

## Running it

**On your physical phone (recommended):**
1. On the phone: Settings -> About phone -> tap "Build number" 7 times to enable Developer Options.
2. Settings -> Developer Options -> enable **USB debugging**.
3. Plug the phone into the laptop via USB, accept the "Allow USB debugging?" prompt on the phone.
4. In Android Studio, the phone should appear in the device dropdown -- press Run (green triangle).

**On the emulator instead:** Android Studio's "Device Manager" -> create a virtual device (any
recent phone profile) -> press Run with that device selected. Slower to start, no physical phone
needed.

## What's real vs. simulated right now

- **Real:** all 5 screens, navigation, the belt compass diagram, settings persistence within a
  session, the emergency acknowledge/escalate timer logic.
- **Real (genuinely, not simulated -- verified on a physical device, Galaxy S25 / Samsung dialer):**
  the phone notification bridge (Notify tab). Grant Notification Access from that screen and
  `NotificationCaptureService` -- a real `NotificationListenerService` -- captures actual phone
  notifications, classifies each one into a priority/pulse pattern (`NotificationMapper`), logs it,
  and feeds it into whichever `BeltRepository` is active so it shows up in Dashboard/Live exactly
  like a sound event would (direction is always UNKNOWN since a phone notification has no spatial
  origin). Confirmed working on-device: ordinary app notifications (OneDrive, Rapido, etc.) come
  through as MEDIUM/double-pulse, and a real incoming call came through as CRITICAL/sustained-buzz
  -- which specifically exercises the fix in `NotificationMapper` that exempts CRITICAL categories
  (call, alarm) from the ongoing-notification filter, since Samsung's dialer marks a ringing call's
  notification ongoing and it would otherwise have been silently dropped. What's still missing is
  the final hop to an actual belt: there is no BLE packet type yet for a phone-originated event
  (`BleBeltRepository.handleNotificationEvent` says so directly), and there are no motors -- so
  today this proves capture + classification + in-app relay, not an actual buzz on a belt.
- **Simulated:** `MockBeltRepository` invents random direction/priority/event data on a timer.
  "Connected" means "the mock started," not a real BLE link.
- **Explicitly not wired (by design, not an oversight):** actually sending an SMS/making a call on
  escalation -- the UI shows what *would* happen, since automatically taking a real-world action
  like that needs deliberate confirmation, not something to bundle into a demo default.

## Next steps once the belt hardware is back
Write `BleBeltRepository : BeltRepository` that scans for the ESP32's BLE service, parses its
characteristic updates into `BeltState`/`BeltEvent`, and swap the single line in
`MainViewModel.kt` that currently instantiates `MockBeltRepository()`.
