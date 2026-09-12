# Haptic Awareness Belt — 4-Mic Spatial Phase: Status Summary

## 1. Hardware architecture

**Decision:** 2 I2S peripherals (I2S0, I2S1), each carrying a stereo pair of INMP441s distinguished by their L/R pin (GND=left slot, 3V3=right slot), confirmed against Espressif's own I2S docs and multiple working INMP441-stereo-pairing references before wiring anything.

```
I2S0 (existing pins, unchanged): FRONT (L/R->GND) + RIGHT (L/R->3V3)
  SCK=GPIO4  WS=GPIO5  SD=GPIO6

I2S1 (new): BACK (L/R->GND) + LEFT (L/R->3V3)
  SCK=GPIO15  WS=GPIO16  SD=GPIO17
```

**Why NOT TDOA/GCC-PHAT:** researched and explicitly ruled out. Each I2S peripheral is its own independent clock master — mics *within* a bus (FRONT+RIGHT, or BACK+LEFT) are genuinely sample-synchronized, but the two buses are not synchronized with each other. Real time-of-arrival direction-finding would need one shared clock feeding both peripherals, which isn't built. Direction is computed from **relative energy**, not phase/timing — a deliberate, documented choice, not a missing feature.

Git checkpoint (`4339c4e`) was taken before any of this started, preserving the working single-mic system untouched.

## 2. Step-by-step, with what happened at each stage

### Step 1 — Raw 4-mic bring-up (Phase F, no direction logic yet)
New `mic_array` module reads both I2S buses in stereo mode, extracts each mic's RMS/peak/ZCR/high-freq-ratio/health independently. **Result:** all 4 mics alive immediately, no zeros, no pinned-max clipping — clean electrical bring-up on the first flash.

### Step 2 — Channel label verification
Flagged an unverified risk in code: which interleaved stereo slot maps to which physical mic wasn't confirmed. **Result:** user tap-tested each mic individually — labels were correct, no swap needed.

### Step 3 — Compass visualization tool built
`tools/compass_view.py`: 4 nodes around a center point, sized/colored by relative loudness — needed since watching raw scrolling numbers wasn't practical for a spatial problem.

### Step 4 — Found FRONT/BACK sensitivity bias
Ambient noise alone made FRONT/BACK dominate the compass regardless of real direction. **Diagnosis:** physical mic-to-mic sensitivity variance (expected — MEMS datasheets specify ±1-3dB unit tolerance), not a wiring fault, confirmed because RIGHT/LEFT *did* light up correctly when sound was aimed directly at them.

### Step 5 — Per-mic calibration implemented
`micArrayCalibrate()`: ~3s quiet-room averaging at boot, per-mic gain = (average of all 4 mics) / (that mic's average), applied to rms/peak/avgAbs going forward. **Result:** mostly fixed; one follow-up case where FRONT stayed elevated turned out to need a clean recalibration (the first calibration window wasn't actually silent).

### Step 6 — Direction algorithm (`spatial_detector` module)
- Each mic gets its own adaptive baseline (EMA, frozen during active events, same pattern as the single-mic system).
- `normalizedEnergy = calibratedRMS / thatMic'sOwnBaseline`, then temporally smoothed.
- Direction = ranked mics; the leader needs to beat the runner-up by a **dominance margin** (not just be `max()`) to claim a single direction (FRONT/RIGHT/BACK/LEFT); a smaller lead produces a merged label (e.g. `FRONT-RIGHT`); opposite-pair ties (FRONT vs BACK) fall to `UNKNOWN` rather than a nonsensical merge.
- **Global vs. local:** if all 4 mics rise together (high mean, low spread across them) it's classified `OMNIDIRECTIONAL` instead of guessing a side.
- Every decision carries a `directionConfidence` (0-1), not just a bare label.

### Step 7 — Integration with the existing (untouched) single-mic pipeline
Rather than duplicating event-detection logic per mic, an "aggregate" `AudioFrame` is built from the currently-loudest mic(s) and fed into the already-validated `environment_model -> event_detector -> temporal_reasoner -> priority_engine` chain unchanged. **Result:** first combined dashboard showing `EVENT` + `DIRECTION` + `PRIORITY` together, matching the original milestone (`sound from RIGHT -> EVENT DETECTED -> DIRECTION = RIGHT`).

### Step 8 — Direction accuracy problems found and fixed
- **Frequent false `OMNIDIRECTIONAL`:** root cause was that `GLOBAL_MEAN_THRESHOLD`/`GLOBAL_CV_THRESHOLD` were never recalibrated after an earlier sensitivity pass on the (separate) event thresholds. Tightened both, and lowered the dominance margin needed to commit to a single direction. **Result:** direction committed more readily afterward.
- Verified physical mic spacing (~30cm per axis, i.e. a 750ml bottle length) is actually sufficient — back-of-envelope inverse-distance math for a 1m source gives ~26% relative amplitude margin, comfortably above the tuned dominance margin. Confirmed this was a software calibration gap, not a hardware/geometry limit.

### Step 9 — High-pitched sounds not detected at 1m
**Root cause:** the event-score fusion weighted raw loudness (energy) far more than acoustic "novelty" (ZCR / high-frequency-ratio deviation from background) — but a high-pitched tone often doesn't have dramatically higher RMS than ambient noise; it's different in *character*, not necessarily louder. **Fix:** doubled the spectral/novelty weight (0.15→0.30) and lowered its saturation point, so timbral novelty alone can now carry a detection instead of needing loudness to prove it.

### Step 10 — Priority calculation was direction-blind
User's critique, and a real bug found in the process: the `>>> REPEATED ACTIVITY <<<` announcement was driven by `temporal_reasoner`'s old **direction-agnostic** counter (any onset, any direction), while a separate mechanism was needed to actually make priority reflect *same-direction* recurrence and *approaching* trends. **Built:** new `direction_memory` module tracking onsets per direction, computing same-direction repeat count and an approaching/receding trend (rising/falling event score across recent same-direction onsets). Unified the announcement and the priority calculation to use this single, direction-aware source. **Result:** repeated same-direction taps now visibly move the priority tier; unrelated sounds from different directions no longer falsely count as "repeated activity."

### Step 11 — Sound between two mics went undetected
**Root cause:** event detection only ever looked at the single loudest mic's own signal. A sound sitting directly between two mics splits its energy across both, so *neither* alone showed as strong a deviation as a directly-aimed sound would, even though the total energy reaching the belt was the same. **Fix:** the aggregate frame now sums the **top 2** mics' amplitude (rms/peak/avgAbs) instead of using only the single loudest one; timbre features (zcr/high-freq-ratio) still come from the single dominant mic since those aren't physically additive. **Result:** user confirmed working correctly afterward.

### Step 12 — Research pass (companies + academic studies)
Compared against Neosensory Buzz, Apple/Google's phone-based sound recognition, and academic DHH-user field studies. Key findings written up separately in `docs/research_and_recommendations.md` — most importantly: **no major commercial product gives real-time direction** (confirming this project's actual differentiation), and a real 3-week field study measured genuine alert fatigue (~112 notifications/day, users progressively ignoring them), which is a concrete risk given this session's tuning has consistently favored sensitivity over precision.

## 3. Current algorithm, end to end

```
4x INMP441 (2 I2S buses, stereo pairs)
  -> per-mic RMS/peak/ZCR/high-freq-ratio/health
  -> per-mic calibration gain (quiet-room boot average)
  -> per-mic adaptive baseline -> normalized energy per mic
  -> rank mics -> dominance-margin direction decision
     (single direction / merged pair / OMNIDIRECTIONAL / UNKNOWN)
  -> direction_confidence
  -> top-2-mic energy fusion -> aggregate AudioFrame
  -> environment_model (dual-timescale baseline, confidence)
  -> event_detector (energy + onset + spectral/novelty fusion) -> eventScore, eventConfidence
  -> temporal_reasoner (BACKGROUND/NOTICE/CANDIDATE/EVENT/COOLDOWN)
  -> direction_memory (same-direction repeat count, approaching/receding trend)
  -> priority_engine (LOW/MEDIUM/HIGH/CRITICAL)
  -> serial dashboard + compass_view.py visualization
```

## 4. Known limitations carried forward
- No true classification yet (`classifyEvent()` still returns `UNKNOWN`).
- No habituation/suppression for a sustained-but-already-acknowledged sound (research-flagged risk, not yet built).
- No per-mode sensitivity (Home/Outdoor/Sleep) — still on the original roadmap.
- Direction is 4-way only (FRONT/RIGHT/BACK/LEFT plus merged pairs), not continuous angle, by design.
- Belt is a daytime/worn-device solution — not a substitute for dedicated sleep-safety bed-shaker systems.
