# Haptic Belt BLE GATT Protocol (v1)

**Status: both sides implemented and verified against real hardware.**
Belt State notifications, Settings writes, Class Upload, and Keyword Upload
have all been exchanged for real between a phone and a real ESP32-S3 belt
(`BleBeltRepository` / `src/ble_server.cpp` + `src/classifier_store.cpp` +
`src/keyword_store.cpp`) — this was not true for most of this document's
life (see git history if the "never connected" caveat is still showing up
elsewhere in old commit messages). This document is the contract both sides
implement so they don't guess at each other's format. Written to be matched,
not treated as final — change it and this file together.

## 1. Encoding choice: fixed-size binary structs, not delimited text

Two options were considered:

- **Delimited text** (e.g. `"FRONT,0.82,0.91,..."`) — human-readable over a BLE
  sniffer, easy to eyeball in `nRF Connect`, no endianness questions.
- **Fixed-size binary struct** — every characteristic is a fixed number of
  bytes at fixed offsets, no parsing/splitting, no allocation, no malformed-
  string edge cases.

**Chosen: fixed-size binary.** Reasons specific to this project:

1. The firmware side is an ESP32-S3 in Arduino/C++ already built around plain
   structs (`AudioFrame`, `SpatialResult`, etc. in `src/*.h`) — packing a
   `struct` (or manually writing bytes at known offsets) is the *native* way
   to produce this data, whereas formatting/parsing CSV-like text on-device
   costs flash, RAM, and `sprintf`/`strtok` edge cases (buffer sizing, locale-
   dependent float formatting) for zero benefit here.
2. Every field in `BeltState` is small, bounded, and already numeric or an
   enum — there's no free-form string data that would justify text framing
   except `classificationLabel`, which is solved below by transmitting a code
   byte instead of the string itself (Section 4.1).
3. Fixed offsets mean the Kotlin side never needs to split/trim/parse-fail-
   handle a string; it just reads bytes at known indices. Fewer failure modes
   on both ends.
4. All three characteristics fit in the **default 23-byte ATT MTU** (20 usable
   payload bytes after the 3-byte ATT header) with room to spare — no MTU
   negotiation is required for this protocol to work, which removes one whole
   class of "did the MTU request actually succeed" bugs during initial
   bring-up.

Trade-off acknowledged: binary payloads are opaque in a generic BLE sniffer
without decoding by hand. Given both sides of this protocol are written by the
same project against this one document, that cost is small and worth paying
for the firmware-side simplicity.

All multi-byte integers are **little-endian** (native byte order for both the
ESP32 (Xtensa/RISC-V, LE) and Android (ARM/x86, LE) — no byte-swapping needed
on either side).

## 2. Service

| | |
|---|---|
| Haptic Belt Service UUID | `81b24d0d-94dd-4ccf-a8b7-b5e24cd00331` |

Randomly generated (v4 UUID), not a reused/standard Bluetooth SIG UUID —
intentional, since this is a fully custom service, not an adopted profile.

Five characteristics, all under this one service:

| Characteristic | UUID | Properties | Size |
|---|---|---|---|
| Belt State | `aa596361-ca3a-4c37-8d74-f8fa8f8b487d` | Read, Notify | 13 bytes |
| Settings | `b8cf1a57-edf2-4912-9c33-c6ce2eba0046` | Read, Write | 4 bytes |
| Acknowledge | `c776acfe-db36-4956-922d-5f13ddaf8584` | Write (with response) | 3 bytes |
| Class Upload | `551f1f19-451e-419a-b181-817bb8997fcc` | Write | up to 88 bytes (variable) |
| Keyword Upload | `88e19205-b3c7-4cfd-8ec2-691dadcdb0ac` | Write | ~55-90 bytes per write, one write per MFCC frame (see Section 8) |

The Belt State characteristic's notifications require enabling the standard
Client Characteristic Configuration Descriptor (CCCD), UUID
`00002902-0000-1000-8000-00805f9b34fb` — this is the one standard/adopted UUID
in the whole protocol, since it isn't something we're free to invent; the BLE
spec requires this exact UUID for notification enable/disable.

## 3. Belt State characteristic (Read, Notify) — 13 bytes

Pushed via notification every time the firmware's own pipeline produces a new
frame result worth telling the phone about (design choice for firmware: either
notify every processed frame, or only on state/value change — either works
with this wire format; not dictating which here since it's a firmware-side
rate/battery trade-off, not a protocol concern). Also Readable so the phone can
pull the current value immediately on connect, before the first notification
arrives.

| Offset | Bytes | Field | Type | Notes |
|---|---|---|---|---|
| 0 | 1 | `protocolVersion` | `uint8` | `1` for this document. Bump on any incompatible layout change. |
| 1 | 1 | `direction` | `uint8` enum | See Section 4.2 |
| 2 | 1 | `directionConfidence` | `uint8` | 0-255 linear for 0.0-1.0 (Section 4.3) |
| 3 | 1 | `eventScore` | `uint8` | 0-255 linear for 0.0-1.0 |
| 4 | 1 | `eventConfidence` | `uint8` | 0-255 linear for 0.0-1.0 |
| 5 | 1 | `awarenessState` | `uint8` enum | See Section 4.2 |
| 6 | 1 | `priority` | `uint8` enum | See Section 4.2 |
| 7 | 1 | `sameDirectionRepeats` | `uint8` | Saturates at 255 (matches `direction_memory.h`'s `int`, truncated to a byte — repeat counts this project has seen are single digits) |
| 8 | 1 | `trend` | `uint8` enum | See Section 4.2. Firmware derives this from `direction_memory.h`'s `isApproaching`/`isReceding` booleans: `isApproaching -> APPROACHING`, `isReceding -> RECEDING`, else `STABLE`. |
| 9 | 1 | `classificationLabel` | `uint8` code | See Section 4.1 |
| 10-11 | 2 | `eventSeq` | `uint16` LE | Monotonically increasing counter, incremented by firmware on every new NOTICE/EVENT onset (wraps at 65535 -> 0). Lets the Acknowledge characteristic (Section 5) unambiguously reference which onset is being acknowledged. |
| 12 | 1 | `reserved` | `uint8` | Always `0` in v1. Reserved for future flags (e.g. mic health / battery) without bumping `protocolVersion`. |

**Not included on the wire:** `lastEventAtMillis` (the app's `BeltState` has
this field). The belt has no reason to know the phone's or its own wall-clock
time; the phone stamps `System.currentTimeMillis()` itself the moment a
notification arrives. This avoids clock-sync entirely.

**Not included on the wire:** `connected`. That's derived on the phone side
from the GATT connection state itself, not a value the firmware reports.

### 3.1 What "aggregate" state means here

The firmware's own pipeline runs event detection on one *aggregate* audio
frame per Section 3 of `docs/four_mic_summary.md` — this characteristic mirrors
that: one direction, one event score, etc. per notification, not four
per-mic vectors. That matches what `BeltState` (the Kotlin model) already
expects and what every screen already renders.

## 4. Shared encodings

### 4.1 `classificationLabel` code table

A single byte code, not a string — `classifyEvent()` in `src/classifier_interface.h`
is a stub that always returns `UNKNOWN` today, so there is exactly one live
value right now, but the table is written out fully so firmware and phone
agree on the codes in advance, before a real classifier exists, instead of
renegotiating the wire format later:

| Code | Label |
|---|---|
| 0 | `UNKNOWN` |
| 1 | `SPEECH` |
| 2 | `ALARM` |
| 3 | `SIREN` |
| 4 | `HORN` |
| 5 | `KNOCK_OR_DOORBELL` |
| 6 | `PHONE_RINGTONE` |
| 7 | `DOG_BARK` |
| 8 | `GLASS_BREAK` |
| 9-255 | Reserved / unassigned -> phone treats as `UNKNOWN` |

Any code the phone doesn't recognize (e.g. a firmware update adds a class
before the app is updated to match) decodes to `UNKNOWN` rather than crashing
or throwing — forward-compatible by construction.

### 4.2 Enum ordinals

Ordinals below **must** match the ordinal order already fixed on both sides
(`src/spatial_detector.h`'s `Direction`, `src/temporal_reasoner.h`'s
`AwarenessState`, `src/priority_engine.h`'s `Priority`, and their Kotlin
mirrors in `app/src/main/java/com/hapticbelt/app/data/BeltModels.kt`) —
transmitting the raw ordinal is only safe because both enums were already
written to match name-for-name. If either enum is ever reordered, this table
(and `protocolVersion`) must be bumped together.

**Direction** (`src/spatial_detector.h` / `BeltModels.kt`):

| Ordinal | Value |
|---|---|
| 0 | FRONT |
| 1 | RIGHT |
| 2 | BACK |
| 3 | LEFT |
| 4 | FRONT_RIGHT |
| 5 | RIGHT_BACK |
| 6 | BACK_LEFT |
| 7 | LEFT_FRONT |
| 8 | OMNIDIRECTIONAL |
| 9 | UNKNOWN |

**AwarenessState** (`src/temporal_reasoner.h` / `BeltModels.kt`):

| Ordinal | Value |
|---|---|
| 0 | BACKGROUND |
| 1 | NOTICE |
| 2 | CANDIDATE |
| 3 | EVENT |
| 4 | COOLDOWN |

**Priority** (`src/priority_engine.h`'s `PRIORITY_*` / `BeltModels.kt`'s `Priority`):

| Ordinal | Value |
|---|---|
| 0 | LOW |
| 1 | MEDIUM |
| 2 | HIGH |
| 3 | CRITICAL |

**Trend** (phone-side only concept, Section 3's derivation rule applies on
the firmware side when building the packet):

| Ordinal | Value |
|---|---|
| 0 | APPROACHING |
| 1 | RECEDING |
| 2 | STABLE |

Any out-of-range ordinal byte (e.g. a future firmware bug, or a version
mismatch) decodes on the phone side to the enum's own `UNKNOWN`/safest member
(`Direction.UNKNOWN`, `Priority.LOW`, `Trend.STABLE`; `AwarenessState` has no
`UNKNOWN` member so it falls back to `BACKGROUND`) rather than crashing.

### 4.3 Float -> `uint8` quantization

All `0.0..1.0` confidence/score fields use `round(value * 255)`, clamped to
`0..255`. Decode is `byte / 255.0f`. This gives ~0.4% resolution, which is far
finer than these scores are meaningfully accurate to (they're heuristic
fusion scores, not measured physical quantities) — 1 byte instead of a 4-byte
IEEE float per field saves 3 bytes × 3 fields = 9 bytes per notification for
no real loss of usable precision.

## 5. Settings characteristic (Read, Write) — 4 bytes

| Offset | Bytes | Field | Type | Notes |
|---|---|---|---|---|
| 0 | 1 | `protocolVersion` | `uint8` | `1` |
| 1 | 1 | `operatingMode` | `uint8` enum | `0`=HOME, `1`=OUTDOOR, `2`=WORKPLACE, `3`=SLEEP (matches `OperatingMode` in `BeltModels.kt`) |
| 2 | 1 | `vibrationIntensity` | `uint8` | 0-255 linear for 0.0-1.0 |
| 3 | 1 | `sensitivity` | `uint8` | 0-255 linear for 0.0-1.0 |

Phone writes this characteristic when the user changes a setting on the
Settings screen; phone also reads it once on connect so the belt (not the
phone) is the source of truth for whatever it was last configured to,
surviving a phone reinstall/app-data-clear.

**Deliberately NOT on this characteristic:** `notificationBridgeEnabled`,
`emergencyContactName`, `emergencyContactPhone`, `acknowledgeTimeoutSeconds`.
All four are phone-only concepts — the notification bridge and the
acknowledge/escalate countdown (see `EmergencyScreen.kt`) run entirely in the
Android app, and the firmware has no use for a phone number or a phone-side
timer value. Sending them over BLE would just be unused bytes the firmware
has to store and echo back for no behavioral effect. If the firmware ever
needs to *act* on the acknowledge timeout itself (e.g. to control its own
vibration pattern while waiting), that's a deliberate future protocol change,
not an oversight here.

## 6. Acknowledge characteristic (Write with response) — 3 bytes

Written once by the phone when the user taps "Acknowledge" on a CRITICAL
alert (`EmergencyScreen.kt`'s `repository.acknowledgeCurrentEvent()`).

| Offset | Bytes | Field | Type | Notes |
|---|---|---|---|---|
| 0 | 1 | `command` | `uint8` | `1` = `ACKNOWLEDGE_EVENT`. Other values reserved/unused in v1. |
| 1-2 | 2 | `eventSeq` | `uint16` LE | Echoes the `eventSeq` from the Belt State notification that triggered the alert being acknowledged. |

**Why echo `eventSeq` back:** without it, a stale acknowledge (e.g. sent right
as a *new* CRITICAL event starts, a race the phone can't fully prevent) could
silently clear the wrong event on the firmware side. With it, firmware can
compare the acknowledged `eventSeq` against whatever its current outstanding
one is and ignore a mismatched/stale ack. **Write with response** (not
write-without-response) is used specifically because this is the one
safety-relevant write in the protocol — the phone should know the belt
actually received the acknowledgment, not just that the local `writeChar...`
call returned.

## 7. Class Upload characteristic (Write) — up to 88 bytes, variable length

**v1.1 addition**, not part of the original v1 spec above (which is why it's
numbered separately and its UUID wasn't picked alongside the other three).
Lets the phone train the belt's sound classifier without a laptop: it
records samples (`SoundTrainingScreen.kt`), computes a Goertzel feature
centroid from them **on the phone** (`GoertzelFeatures.kt`, matching
`src/classifier_interface.cpp`'s on-device feature extraction and
`tools/goertzel.py`'s spec), and writes the result here. Firmware stores it
in `classifier_store` (flash-backed NVS, survives reboot) — no recompile, no
reflash.

| Offset | Bytes | Field | Type | Notes |
|---|---|---|---|---|
| 0 | 1 | `nameLen` | `uint8` | Length of the name that follows, 1-23. |
| 1 | `nameLen` | `name` | ASCII bytes | Not null-terminated on the wire; firmware null-terminates when storing. |
| 1+nameLen | 64 | `centroid` | 16 × `float32` LE | The trained class's Goertzel feature centroid — see `tools/goertzel.py`'s `TARGET_FREQS` for what the 16 values correspond to. |

**Needs a negotiated MTU above the default 23 bytes** — up to 88 bytes total
doesn't fit the default 20-byte payload, unlike the other three
characteristics, which were deliberately designed to avoid needing MTU
negotiation at all (Section 1). `BleBeltRepository` requests MTU 512 right
after connecting (bumped from an earlier 185 once Keyword Upload, Section 8,
needed more); firmware calls `NimBLEDevice::setMTU(512)`.

Writing the same class name again replaces that class's centroid (a re-sync,
not a duplicate); a new name is appended up to `CLASSIFIER_MAX_CLASSES` (8).
A write for a 9th distinct name is silently dropped by
`classifierStoreAddClass()` — the app has no way to know this failed as of
this writing (Write, not Write-with-response, unlike Acknowledge).

## 8. Keyword Upload characteristic (Write) — chunked, one write per MFCC frame

**v1.2 addition, rewritten in v1.3 (chunking) and v1.4 (MFCC + DTW).** Same
purpose as Class Upload (train the belt without a laptop) but for a spoken
keyword/name instead of an environmental sound's timbre — enrolled via
`KeywordEnrollScreen.kt`'s "say it 5 times" flow.

**Why this is chunked, one write per frame, instead of one write of the
whole template:** the original v1.2 design sent the entire template (then a
fixed 472 bytes) in a single write. That was this project's first real BLE
write to real hardware, and it **consistently killed the connection
outright** (GATT status 133) — despite the MTU having correctly negotiated
to 512 beforehand. The lesson: a successfully negotiated large MTU does not
mean a single large write of that size is safe to actually send. The real
root cause turned out to be different and worse than "just shrink the
write": `keywordStoreAdd()` was doing a blocking NVS flash write
synchronously inside the BLE `onWrite` callback (NimBLE's host task, which
is time-critical for holding the connection open) — a flash write slow
enough to starve the stack. The fix was two-part: (1) chunk the upload into
small per-frame writes so no single write is large enough to be risky on
its own, and (2) move the actual flash write out of the BLE callback
entirely, deferred to a flag `keywordStorePersistPending()` checked once per
`main.cpp` `loop()` frame. Both parts were necessary; chunking alone would
not have fixed the underlying stack-starvation cause.

**Why a template is a variable-length sequence of MFCC frames at all** (not
a fixed vector, and not the original Goertzel-bin sequence): a controlled
real-hardware test measured pure silence and continuous real speech of an
enrolled word *separately* and found their (then Goertzel-bin) distances
almost entirely overlapped — no threshold could tell them apart. The
matching approach was rewritten to MFCC (Mel-Frequency Cepstral
Coefficients, `mfcc_dsp.h/.cpp` on the belt, a close mirror in
`KeywordFeatures.kt` on the phone) + DTW (Dynamic Time Warping, `dtw.h/.cpp`)
— the standard technique for few-shot custom keyword spotting with no
training pipeline. MFCC borrows the feature-extraction idea (not the
trained-CNN classifier, which needs a labeled dataset this project doesn't
have) from https://github.com/lbalic21/keyword_spotting. DTW time-aligns
the live utterance against the stored template instead of comparing at
fixed positions, which is why the template no longer needs to be a fixed
length — it's exactly as long as the enrolled word took to say (up to
`KEYWORD_MAX_TEMPLATE_FRAMES` = 60, ~1.44s).

Wire format, one write per segment (segment = one MFCC frame), sent in
order, each write's real result awaited before the next is sent
(`BleBeltRepository.writeCharacteristicWithRetry`, with retries):

| Offset | Bytes | Field | Type | Notes |
|---|---|---|---|---|
| 0 | 1 | `segmentIndex` | `uint8` | 0-based, must arrive in order. |
| 1 | 1 | `totalSegments` | `uint8` | How many frames this template has (variable, 1-60); must match across every segment of the same upload. |
| 2 | 1 | `nameLen` | `uint8` | Length of the name that follows, 1-23. |
| 3 | `nameLen` | `name` | ASCII bytes | Not null-terminated on the wire; sent on every segment (not just segment 0), so the firmware never has to trust a segment belongs to the right in-progress upload without checking. |
| 3+nameLen | 52 | `frame` | 13 × `float32` LE | One MFCC frame: coefficients C1-C13 (C0, the overall loudness term, is deliberately dropped — see `mfcc_dsp.cpp`). |

Total per-write size: `3 + nameLen + 52` bytes (~55-76 bytes for a typical
short name) — still over the default 23-byte ATT MTU, so this still needs
the negotiated MTU 512 (`BleBeltRepository.requestMtu`, `NimBLEDevice::
setMTU(512)`), just not because of one huge write anymore.

The belt (`ble_server.cpp`'s `KeywordUploadCallbacks`) accumulates segments
into a per-upload buffer and only calls `keywordStoreAdd()` once the final
segment (`segmentIndex == totalSegments - 1`) arrives. It silently drops the
in-progress upload if a segment arrives out of order, with a different
`totalSegments` than segment 0 declared, or that doesn't match the name
segment 0 started — rather than risk stitching together frames from two
different enrollments.

Phone-side template construction: 5 enrollment recordings, each run through
energy-based onset **and offset** detection (`KeywordFeatures.extractTemplate`)
to capture only the actual utterance, producing 5 variable-length MFCC
sequences. Rather than average them (which doesn't apply cleanly to
different-length sequences the way it did for the old fixed-length
vectors), `KeywordFeatures.selectBestTemplate()` picks the **medoid** — the
recording whose average DTW distance to the other 4 is lowest — as the one
actually uploaded. A cheaper, simpler stand-in for a true DTW barycenter
average.

On-device matching (`keyword_detector.cpp`) uses energy-based speech
endpointing (an onset/offset state machine, closely mirroring the phone
side's, with the ambient-noise baseline frozen while actively "speaking" —
the same principle `environment_model.cpp` already uses) so DTW only ever
runs once a complete utterance has been detected and ended, never against a
blind rolling window of whatever's currently in the mic buffer (silence
included) the way the old Goertzel approach did. Cepstral mean normalization
(subtracting an utterance's own mean MFCC vector) is applied on both sides
to reduce sensitivity to enrollment-vs-runtime volume/mic-gain differences.

Storage: `keyword_store` (flash-backed NVS, `KEYWORD_MAX_COUNT` = 4 keywords,
same re-sync-by-name / silent-drop-at-capacity behavior as Class Upload; the
NVS blob layout changed to two separate keys, count and data, instead of one
combined blob — see `keyword_store.cpp`'s own history comment for why).

**Not wired into `priority_engine`** — but a match *does* now reach the
phone: a match pulses the Belt State characteristic's previously-always-0
reserved byte 12 with the matched keyword's slot index (one-shot, single
frame), decoded in `BleProtocol.kt` and surfaced on the Dashboard's Keyword
card. Still not fed into `priority_engine` or `classificationLabel` — that
still needs a protocol decision (a new classification code, or a separate
signal path) deliberately not made unilaterally.

**Honest status on matching quality**: the pipeline end-to-end (enrollment →
chunked upload → flash storage → on-device DTW inference → BLE relay →
Dashboard display) is verified working on real hardware. `MAX_MATCH_DISTANCE`
in `keyword_detector.cpp` is **not yet validated against real speech** —
it's currently a synthetic-signal-informed guess (see `tools/mfcc_dtw.py`),
not a live-tuned number. A real silence-vs-speech test against actual
hardware (same protocol that caught the old Goertzel approach's failure)
still needs to run before this is trustworthy.

## 10. What firmware is expected to do with this (out of scope of the Android side, noted for whoever implements it next)

- Populate a `HapticBeltService` with the five characteristics above, at the
  UUIDs given.
- Fill and notify the Belt State characteristic from the existing pipeline's
  output (`SpatialResult.direction`/`directionConfidence`,
  `TemporalResult.state`, `Priority` from `evaluatePriority()`,
  `DirectionMemoryResult` for `sameDirectionRepeats`/trend,
  `classifyEvent()`'s result mapped through Section 4.1's table).
- Apply writes to the Settings characteristic to whatever the firmware-side
  equivalent of operating mode / vibration intensity / sensitivity ends up
  being.
- On an Acknowledge write with a matching `eventSeq`, stop whatever
  vibration/LED pattern was signaling the unacknowledged CRITICAL event.
- Store Class Upload writes in flash so a trained classifier survives reboot.
- Store Keyword Upload writes in flash the same way, and continuously match
  live audio against them independent of the amplitude-based state machine.

**Status: all of the above is implemented and verified against real
hardware** (`src/ble_server.h/.cpp`, `src/runtime_config.h/.cpp`,
`src/classifier_store.h/.cpp`, `src/classifier_interface.cpp`,
`src/keyword_store.h/.cpp`, `src/keyword_detector.h/.cpp`,
`src/mfcc_dsp.h/.cpp`, `src/dtw.h/.cpp`). Belt State notifications, a
Settings write, and a full Keyword Upload (chunked, all segments) have each
been confirmed reaching a real belt from the real app — including a genuine
keyword match reaching the Dashboard within about a second of being spoken.
Acknowledging a CRITICAL event does stop its continuous LED pulse
(`awareness_output.cpp`'s `acknowledgeCriticalAlert()`), and the Ack
callback does reject a stale/mismatched `eventSeq` (`ble_server.cpp`'s
`AckCallbacks`) — both implemented but **not yet exercised against a real
CRITICAL event on real hardware** (nothing has triggered one live yet). A
keyword match reaching the Belt State characteristic is done (Section 8);
reaching `priority_engine`/`classificationLabel` is still deliberately not
done, pending a protocol decision.

## 11. Explicitly not claimed anywhere in this document

Per this project's own established norm (see the "not fabricated" section at
the end of `docs/spatial_architecture_research.md`): this document does not
claim any tested connection *range* (only "works at close range, same
room"), latency, throughput, or power-consumption figure. It also does not
claim the keyword-matching approach (Section 8) reliably discriminates
speech from silence, or one enrolled word from another — the MFCC+DTW
rewrite fixed a proven failure of the previous approach (Goertzel-bin
distances for real silence and real speech almost entirely overlapped, per
a controlled test) and is verified to work end-to-end as a *pipeline*, but
its actual matching threshold (`MAX_MATCH_DISTANCE`) has not yet been
validated against a live silence-vs-speech test the way the previous
approach's was. Firmware acknowledging a real (not simulated) CRITICAL event
round-trip has also not been exercised live. Direction/spatial accuracy
figures, and Class Upload's on-device classification accuracy once trained,
remain likewise unvalidated against real hardware.
