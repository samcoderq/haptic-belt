# Haptic Awareness Belt — Single-Mic Intelligence Prototype: Status Summary

**Purpose of this document:** a complete handoff/reference on what was built, tested, broken, and fixed
during the single-microphone phase, so the next phase (4-mic direction system) or a fresh
conversation can pick up with full context.

---

## 1. Hardware state (unchanged throughout, verified working)

- **Board:** ESP32-S3-DevKitC-1 style board (ESP32-S3-WROOM module, two USB-C ports)
- **Mic:** 1× INMP441 I2S MEMS microphone (only one connected — 4-mic array not yet wired)
- **Wiring (do not change without reason):**
  ```
  INMP441 VDD  -> 3V3
  INMP441 GND  -> GND
  INMP441 SCK  -> GPIO4
  INMP441 WS   -> GPIO5
  INMP441 SD   -> GPIO6
  INMP441 L/R  -> GND  (left channel)
  ```
- **Toolchain:** VS Code + PlatformIO (`platform = espressif32`, `framework = arduino`, board `esp32-s3-devkitc-1`)
- **Critical non-obvious config:** `build_flags = -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1` in
  `platformio.ini` — without this, `Serial` silently routes to UART0 (GPIO43/44) instead of the
  native USB-Serial/JTAG port you actually flash/monitor through. This cost significant debugging
  time (see Section 3) before being identified.
- Program via the **"USB"**-labeled port (not "UART" — using the wrong port loses the serial monitor
  entirely with the CDC-on-boot flag set).

## 2. Final architecture (modular, all in `src/`)

```
audio_capture      -> I2S acquisition + RMS/peak/ZCR/high-freq-ratio + mic health
environment_model  -> dual-timescale baseline (fast ~5s / slow ~50s EMA), baseline confidence,
                      environment-change detection
event_detector     -> fuses energy + onset + spectral deviation into eventScore (0-1) and
                      eventConfidence (0-1, agreement across sub-signals)
temporal_reasoner  -> state machine: BACKGROUND -> NOTICE/CANDIDATE -> EVENT -> COOLDOWN,
                      hysteresis, repetition detection (PERIODIC/IRREGULAR)
priority_engine    -> LOW/MEDIUM/HIGH/CRITICAL from score+confidence+persistence+repetition
classifier_interface -> stub, always returns UNKNOWN (no trained model exists)
config.h           -> every tunable constant, single source of truth
main.cpp           -> orchestrates the pipeline, prints the dashboard
```

Live visualization tool: `tools/live_plot.py` (Python, matplotlib) — plots RMS/Baseline, Event
Score with threshold lines, and the literal state as a step trace. Reads the same serial
stream, doesn't require the firmware to change.

## 3. Chronological journey — what failed, what we learned, what fixed it

### 3.1 Getting *any* signal at all
- **First attempt:** uploaded fine, but **zero serial output at all**, despite the board clearly
  running (LED, no crash). Root cause: `Serial` was bound to UART0 (GPIO43/44), but we were
  plugged into the native USB-Serial/JTAG port used for flashing. **Fix:** added
  `ARDUINO_USB_CDC_ON_BOOT=1` build flag to route `Serial` onto the same USB connection.
- **Then:** `RMS: 0.0 / PEAK: 0.0`, flat, forever. Root cause: mic wasn't physically wired yet.
- **Then, after wiring:** RMS jumped between near-zero and a hard-pinned digital maximum
  (`PEAK: 131072`, repeating) — a classic loose-connection/floating-pin symptom, not real sound.
  **Fix:** reseated jumpers (especially SD and L/R). Confirmed via a wiggle-test methodology.
- **Then, briefly:** RMS got *stuck at exactly `1.0`* for 30+ consecutive frames — a different bad
  sign (real audio always has tiny self-noise variation; a frozen constant means a line isn't
  toggling). Traced to a wire not fully seated after the previous reseat. Fixed with a firmer
  reseat and verified with a physical tap test (mechanical shock should always move the reading).
- **Success point:** RMS became continuously, naturally varying, baseline settled to a stable
  number in a quiet room, confidence climbed to 1.00. First fully verified good hardware state.

### 3.2 The horn test — first real detection logic failure
- Played horns continuously for ~1 minute (with gaps). **Result: 0 events detected**, despite
  clearly audible loudness spikes up to 7x the quiet baseline.
- **Root cause (two things):**
  1. Baseline adapts during BACKGROUND state — a horn played *continuously* gets absorbed as "new
     normal" over ~5s, which is *correct* behavior for a fan/AC turning on, but wrong for a
     repeated alarm-like sound. This is a known, documented limitation (see Section 5).
  2. The real killer: peak instantaneous score during the loudest moment was **0.71**, but
     `INSTANT_THRESHOLD` was **0.8** — a single-frame spike (a honk is short) never had a second
     consecutive frame to satisfy the persistence path either. **Fix:** lowered `INSTANT_THRESHOLD`
     to 0.65.
- Built `tools/live_plot.py` at this point specifically to stop guessing from text logs and see
  the actual RMS/score/state timeline.

### 3.3 "Is it actually staying in EVENT while I shout continuously?"
- The graph only plotted `Event Score`, not literal `State` — a dip in score didn't necessarily
  mean it exited EVENT (exit requires dropping below `DEACTIVATE_THRESHOLD`, not below the entry
  bar). **Fix:** added a third graph panel plotting the actual state as a step trace, so this is
  now directly visible instead of inferred.

### 3.4 The "single mic intelligence" rewrite (biggest single change)
Triggered by finding that a **distant tap** and a **nearby weak tap** were both just silently
invisible — the old system was strictly binary (BACKGROUND or EVENT), with no middle ground.
Rebuilt as the modular pipeline in Section 2: added a `NOTICE` tier, baseline confidence based on
measured recent variance (not just a rise/fall heuristic), dual-timescale environment-change
detection, event confidence (sub-signal agreement), repetition tracking, and a priority engine.

- **Compile failure during this rewrite:** `enum class Priority { LOW, ..., HIGH, ... }` collided
  with Arduino's `#define LOW 0` / `#define HIGH 1` macros (used for `digitalWrite`) — the
  preprocessor silently mangled the enum body before the compiler ever saw it. **Fix:** renamed to
  `PRIORITY_LOW` / `PRIORITY_HIGH` etc.
- **Post-flash bug caught by self-review:** both baselines were seeding off the classic I2S
  first-read startup-pop artifact (`Baseline: ~20000`, `SlowBaseline: ~67000` in a room where real
  RMS was ~1000). Since the slow baseline has a ~50s time constant, a bad seed would take a long
  time to recover. **Fix:** discard the first 3 frames after `audioInit()` before anything seeds
  off them.

### 3.5 UX correction: "don't annoy a deaf person"
User's own critique, and the most important product-level fix in this phase: a small/mid sound
*close* to the mic was hitting NOTICE from a single ~100ms blip (footstep, paper shuffle) — pure
noise/annoyance. Meanwhile a genuinely important sound *far away* also only reached NOTICE,
because raw amplitude cannot distinguish "far and important" from "near and trivial" — that is a
**real, physical limitation of single-mic amplitude-only sensing**, stated honestly rather than
patched over.
- **Fix implemented:** NOTICE now requires ~500ms of sustained elevation before it's ever
  announced (filters one-off nearby blips). Priority is now computed for NOTICE too, driven by
  *persistence and repetition* rather than loudness, and re-announces periodically so a sustained
  quiet/distant sound can escalate in priority over time instead of being reported once at LOW and
  going silent.
- **Explicitly not solved:** true "importance regardless of distance" needs either real sound
  classification (recognizing a siren/alarm signature) or multi-mic spatial/distance cues — both
  are future work, not deliverable with one mic and no classifier.

### 3.6 Talking + shout test — first clean success
3 deliberate shouts against a continuously noisy talking background: **all 3 correctly registered
as EVENT**, and the background talking itself did NOT spam false events (two loud syllables
touched CANDIDATE briefly but correctly failed to confirm and fell back to BACKGROUND). This
validated that the fused energy+onset+spectral scoring, combined with baseline tracking, can tell
"a shout on top of noise" apart from "the noise itself," which was the core goal of moving beyond
raw RMS-threshold detection.

### 3.7 The real-world range problem (final, most consequential fix)
Metal-plate hit test at multiple distances:
- **~1m: scored 0.85–0.93** → confirmed EVENT every time.
- **~3-4m: scored only 0.35–0.55** → completely filtered out, no NOTICE, no EVENT, nothing.

This was flagged as the core usability failure: **a deaf user needs distant, clearly audible
sounds to register, not just sounds right next to the belt.** All four thresholds were
recalibrated downward by roughly half, directly from this measured data:

| Threshold | Old | New |
|---|---|---|
| NOTICE_ENTER | 0.20 | 0.12 |
| CANDIDATE | 0.50 | 0.25 |
| INSTANT | 0.65 | 0.35 |
| DEACTIVATE | 0.35 | 0.15 |

**Result after retest: confirmed working** ("works like a charm" — user's own words) for the
distances tested so far.

**Explicit caveat carried forward:** this was calibrated against 1m and 3-4m data only. The
original ask was 6-7m; that distance has not been empirically tested. If a hit at that range
produces a signal below the mic's real noise floor, no amount of threshold tuning will recover
it — that's a hardware/physics limit, not a software one, and needs to actually be measured
before claiming it works.

## 4. Current full parameter snapshot (`config.h`)

```
BASELINE_FAST_ALPHA = 0.02      (~5s time constant)
BASELINE_SLOW_ALPHA = 0.002     (~50s time constant)
NOTICE_ENTER_THRESHOLD = 0.12
NOTICE_EXIT_THRESHOLD  = 0.06
CANDIDATE_THRESHOLD    = 0.25
INSTANT_THRESHOLD      = 0.35
DEACTIVATE_THRESHOLD   = 0.15
CONFIRM_FRAMES         = 2       (~200ms persistence path)
NOTICE_SUSTAIN_FRAMES  = 5       (~500ms before a weak signal is ever surfaced)
COOLDOWN_FRAMES        = 8       (~800ms)
REPEAT_WINDOW_FRAMES   = 100     (~10s)
REPEAT_COUNT_THRESHOLD = 3
DEVIATION_FOR_SCORE_1  = 6.0
WEIGHT_ENERGY/ONSET/SPECTRAL = 0.60 / 0.25 / 0.15
PRIORITY_MEDIUM/HIGH/CRITICAL_CUT = 0.30 / 0.60 / 0.85
```

## 5. Known limitations (stated plainly, not hidden)

1. **No FFT.** "Spectral" features are zero-crossing rate + a one-pole high-pass energy ratio —
   cheap proxies for timbre, not a real spectral centroid/flux. Good enough to bias toward
   broadband/impulsive sounds; not a substitute for real frequency analysis.
2. **No sound classification.** `classifyEvent()` always returns `UNKNOWN`. Nothing knows the
   difference between a siren, a horn, and a shout.
3. **Priority is acoustic significance only** — explicitly not a validated real-world danger
   assessment. CRITICAL requires sustained persistence + repetition by design; it cannot be
   reached from a single loud instant alone.
4. **Sensitivity vs. false positives is now tilted toward sensitivity.** Lowering thresholds for
   range means louder ordinary background activity (talking, laughing) is more likely to
   false-trigger than before. Not yet stress-tested in a genuinely busy/loud environment.
5. **A continuous repeated sound (like a real alarm) vs. an environment that got permanently
   louder (like a fan) are still not reliably distinguished** — both look like "sustained elevated
   level" to the baseline model. Partially mitigated by repetition tracking, not solved.
6. **Single mic — zero direction information.** That's the entire point of the next phase.
7. **6-7m detection range is unverified.** Only 1m and 3-4m have real data.
8. **Slow baseline needs up to ~60s after a cold boot** to fully settle (by design, 50s time
   constant) — don't judge environment-change detection in the first minute after power-on.

## 6. Where this leaves the decision for what's next

The originally planned next step is the 4-microphone phase:
```
4x INMP441 -> synchronized acquisition -> per-mic signal quality -> relative energy
  -> cross-mic correlation -> direction estimation (FRONT/RIGHT/BACK/LEFT) -> haptic motor mapping
```

Worth noting: multiple mics could *also* partially address the still-open "importance vs.
distance" ambiguity from Section 3.5/3.7 (e.g., comparing relative energy across mics gives a
rough proximity/direction cue that a single mic fundamentally cannot provide), so that work isn't
wasted effort separate from the range problem — it's a natural continuation of it.

The other still-open thread is real sound classification (Section 5, item 2), which is the actual
fix for distinguishing sound *type/importance* independent of loudness — still not started.
