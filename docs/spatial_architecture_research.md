# Spatial Direction-Estimation Architecture: Research & Engineering Report

**Status: research only. No code changed. Report precedes any implementation decision, per instruction.**

Throughout this document: **[CALC]** = my own calculation from our actual geometry, **[LIT]** = a claim
directly supported by a cited source, **[INFER]** = engineering judgment/inference not directly
tested, **[EXP]** = a proposed experiment, not yet run.

---

## 1. Executive conclusion

The long-range direction failure is **primarily physics, not a software bug** — the amplitude
difference between mics genuinely collapses toward zero as distance grows, on a small ~30cm
aperture. Adaptive-baseline normalization (investigated in Phase 1) is a secondary, real, but
smaller contributor. Physical acoustic shaping (hoods/horns) is **only worth prototyping for
high-frequency content**, because a belt-appropriate small structure is physically incapable of
shaping directivity for typical speech/alarm/horn fundamentals — their wavelengths are far larger
than any hood we can build. The single most promising software direction, backed by a directly
relevant published comparison **[LIT]**, is that **sound-intensity/energy-based localization
degrades less than TDOA/GCC-PHAT in reverberant conditions for compact arrays** — meaning our
existing energy-based approach is *already* the literature-favored method class for this array
size, and the priority should be refining it (frequency-band features, better temporal handling)
before adding a physical structure or full TDOA.

## 2. Why current long-range direction estimation fails

Three contributing mechanisms, not mutually exclusive:

- **[CALC] Geometric SNR collapse (dominant).** See Section 3 — the true amplitude ratio between
  mics shrinks from a ~2.6dB best case at 1m to ~0.26dB at 10m. Real-world mic self-noise and
  calibration residual error are typically on that same order, so the directional signal is
  swamped by noise floor at distance, independent of any algorithm.
- **[INFER, partially tested in Phase 1] Adaptive baseline drift.** Each mic's baseline can
  differentially absorb weak/sustained signal before an aggregate freeze triggers, shrinking
  exactly the ratio meant to reveal direction. Phase 1 diagnostics were built to test this
  directly; results pending review.
- **[LIT] Reverberation.** Indoor reflections degrade both amplitude-ratio and TDOA methods, but
  by different mechanisms and to different degrees (Section 8).

## 3. Quantitative aperture/distance analysis [CALC]

Modeling the belt as mics at radius r=0.15m from center (30cm diameter), speed of sound c=343 m/s.
Amplitude follows the far-field inverse-distance law (pressure ∝ 1/distance), not inverse-square.

**Opposite pair (FRONT vs BACK), source exactly on-axis** — near mic distance = D−r, far mic = D+r:

| Distance D | Near dist | Far dist | Amplitude ratio | Level difference |
|---|---|---|---|---|
| 1 m | 0.85 m | 1.15 m | 1.353 | **2.63 dB** |
| 2 m | 1.85 m | 2.15 m | 1.162 | 1.30 dB |
| 3 m | 2.85 m | 3.15 m | 1.105 | 0.87 dB |
| 5 m | 4.85 m | 5.15 m | 1.062 | 0.52 dB |
| 10 m | 9.85 m | 10.15 m | 1.030 | 0.26 dB |

**Adjacent pair (FRONT vs RIGHT, our actual synchronized I2S0 pair), source exactly at FRONT:**

| Distance D | FRONT dist | RIGHT dist | Amplitude ratio | Level difference |
|---|---|---|---|---|
| 1 m | 0.85 m | 1.011 m | 1.190 | **1.51 dB** |
| 2 m | 1.85 m | 2.006 m | 1.084 | 0.70 dB |
| 3 m | 2.85 m | 3.004 m | 1.054 | 0.46 dB |
| 5 m | 4.85 m | 5.002 m | 1.031 | 0.27 dB |
| 10 m | 9.85 m | 10.001 m | 1.015 | 0.13 dB |

**Reading these numbers:** at 1m, even the weaker (adjacent-pair) case gives 1.5dB — comfortably
measurable. At 5m, the best case (opposite pair) is 0.52dB and the adjacent-pair case is 0.27dB —
both are inside or near the noise floor of a real, calibrated-but-imperfect MEMS mic system. This
is the core, unavoidable physics: **the geometric answer to "why does it fail at range" is that
the signal we're measuring approaches zero long before the sound itself becomes inaudible.**

**Important asymmetry [CALC]:** the raw path-length difference for the on-axis opposite-pair case
is exactly 2r = 30cm at *any* distance (not distance-dependent, in this idealized straight-line
geometry) — it's the *amplitude ratio*, not the raw path difference, that collapses with distance.
This matters directly for Section 4.

## 4. TDoA feasibility analysis [CALC + LIT]

**[CALC]** Because path-length difference (and thus time delay) for a far-field source is
approximately distance-independent (Δt ≈ baseline·cosθ / c), the *delay itself* does not shrink
with distance the way the amplitude ratio does. For our synchronized FRONT-RIGHT pair (baseline
21.2cm, computed as r√2), a source exactly at compass-FRONT sits at 45° to that pair's axis:

```
delay = baseline × cos(45°) / c = 0.212 × 0.707 / 343 ≈ 437 μs ≈ 7 samples at 16kHz
```

This ~7-sample delay is, in principle, constant regardless of source distance (far-field
approximation) — a real theoretical advantage over amplitude ratio.

**[LIT] But this advantage is not free in practice.** GCC-PHAT and time-domain correlation degrade
sharply as SNR drops below ~10dB, and specifically **"band-limited and normalized GCC provides
better estimations for short distances... its performance is negatively affected by increases in
these distances"** — [Speaker Localization in Classroom Environments Using GCC-PHAT](http://www.apsipa.org/proceedings/2025/papers/APSIPA2025_P234.pdf). So while the underlying
delay doesn't shrink geometrically, **measuring it reliably still degrades with distance**,
because the source excites proportionally less signal at the mic as distance grows, and ambient/
self-noise doesn't. Both methods lose at long range — for different underlying reasons (geometry
vs. SNR) — which is an important nuance the naive "ToA doesn't have this problem" framing misses.

**Resolution limit [CALC]:** max possible delay within our pair (source exactly on the pair's own
axis) is baseline/c = 0.212/343 ≈ 618μs ≈ **9.9 samples at 16kHz**. This is a coarse, low-resolution
measurement — we should never claim more than "leans toward FRONT vs. RIGHT," never a precise angle.

**Hardware constraint (already established, unchanged):** only FRONT↔RIGHT and BACK↔LEFT are
sample-synchronized (same I2S bus/clock). Cross-bus pairs cannot be timed without a shared clock,
which doesn't exist on this board as wired.

## 5-9. Research findings

**[LIT] Small-aperture arrays, general limitations:** *"The physical size of microphone arrays is
constrained, with small apertures restricting spatial resolution."* Far-field assumption is
*required* for time-delay methods and becomes questionable at close range for a small array —
[Small Aperture Antenna Arrays for DOA Estimation](https://www.ncbi.nlm.nih.gov/pmc/articles/PMC12196855/) (note: this is antenna/RF literature; the
underlying wave mathematics parallels acoustics, but I'm flagging the domain difference rather
than presenting it as acoustically validated).

**[LIT] — the key research lead, confirmed and directly relevant:** ["Comparison of the sound
source localization methods appropriate for a compact microphone array"](https://koreascience.or.kr/article/JAKO202011263332658.page), Journal of the
Acoustical Society of Korea, 2020, and the closely related [**"Combined microphone array for
precise localization of sound source using the acoustic intensimetry,"** Mechanical Systems and
Signal Processing, 2021](https://www.sciencedirect.com/science/article/abs/pii/S0888327021002156) (I could not access the full paywalled text; citing the confirmed
title/venue/year and the search-indexed abstract data):
- Mean localization error at T60=0.4s (moderate reverberation): **acoustic intensimetry 2.9° vs.
  GCC-PHAT 7.3°**
- At T60=1.0s (heavy reverberation): **intensimetry 9.9° vs. GCC-PHAT 13.0°**
- Explicitly stated advantage: *"the acoustic intensity vector method has an advantage of
  downsizing the array layout due to small finite-difference error when microphone spacing is
  short"* — i.e., intensity/energy methods are specifically well-suited to **small** arrays, not
  a fallback forced by our constraints.

This directly answers the "key research lead" the brief asked me to investigate: **the principle
does apply to our situation** — we have a small (30cm), reverberant-room, wearable array, which is
exactly the regime where this literature says energy/intensity-based localization holds up better
than TDOA-family methods, not worse.

**[LIT] GCC-PHAT reverberation/noise sensitivity, independently confirmed:** *"GCC-PHAT achieves
unbiased, optimal delay estimation in the absence of noise and reverberation, but its performance
deteriorates sharply in realistic scenarios,"* and degrades further below ~10dB SNR — [Neural
GCC-PHAT for Robust Time-Delay Estimation](https://www.emergentmind.com/topics/neural-generalized-cross-correlation-ngcc-phat).

**[LIT] Acoustic horns/baffles — directivity is fundamentally frequency-limited by structure size:**
*"a horn can only offer directivity control down to frequencies where the wavelength is comparable
to horn mouth size"* — [Constant directivity acoustic horn, US Patent 7044265](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/7044265). Also: *"enlargement
of a microphone baffle diameter can improve sensitivity, but causes a diffraction effect in the low
frequency band, which sometimes degrades directional frequency response"* (same source family).
**[CALC] applying this to our constraint** ("shallow, lightweight, belt-appropriate" — no giant
funnel): a realistic hood is perhaps 3-6cm across. Directivity control would only meaningfully
begin at wavelengths ≤ that size, i.e. frequencies ≥ 343/0.06 to 343/0.03 ≈ **5.7–11.4 kHz**. Most
speech (300Hz-3.4kHz), car horns (~300-500Hz fundamental), and most alarm/siren fundamentals
(~500Hz-3kHz) sit well below this — **a small hood would do essentially nothing for these**, and
would only plausibly help with high-pitched content (which happens to be the class we specifically
improved detection for earlier this session via the spectral/novelty scoring, not via hardware).

**[LIT] Near/far-field boundary (Fraunhofer distance), formula confirmed:** boundary = 2D²/λ where
D = aperture — [Fraunhofer and Fresnel Distances](https://www.researchgate.net/publication/317394255_Fraunhofer_and_Fresnel_Distances_Unified_derivation_for_aperture_antennas) (again, antenna-domain
source; applying the formula to our acoustic case is my own extension, not a claim the paper
studied acoustics). **[CALC] applying it to our D=0.30m belt:**

| Frequency | Wavelength | Far-field boundary (2D²/λ) |
|---|---|---|
| 200 Hz | 1.72 m | 0.11 m |
| 500 Hz | 0.69 m | 0.26 m |
| 1000 Hz | 0.34 m | 0.53 m |
| 2000 Hz | 0.17 m | 1.05 m |
| 4000 Hz | 0.086 m | 2.10 m |
| 8000 Hz | 0.043 m | 4.20 m |

**This is an important, non-obvious finding [CALC]:** for higher-frequency content (2-8kHz — where
alarms and many speech consonants live), our 1-5m test range is *still inside or at the edge of
the near-field/transition zone*, meaning the simple far-field constant-delay assumption used in
Section 4 isn't cleanly valid there either. Low frequencies (200-500Hz — car horn fundamentals)
are safely far-field even at 1m. **Different frequency bands are in genuinely different physical
regimes at the same real-world distance** — this is a concrete, calculated reason (not just a
guess) to take the frequency-band question (Section 14) seriously.

**[LIT] Wearable/belt-mounted precedent — directly relevant prior art found:**
[**SmartBelt: A Wearable Microphone Array for Sound Source Localization with Haptic
Feedback**](https://arxiv.org/pdf/2202.13974) (Michaud, Moffett, Tapia Rousiouk, Duda, Grondin;
arXiv, Feb 2022) — a belt-worn mic array with directional haptic motors, calibrated per waist
size, is essentially the same concept as this project. Reported results: **2.90° mean angular
error, 92.3% haptic-motor-selection accuracy**. I could not extract their exact mic count,
algorithm, or distance-robustness data from the accessible portions of the paper (the PDF I could
fetch didn't yield readable body text) — flagging this as a gap, not something I'm papering over.
This is worth someone manually reading in full; it's the closest thing to a direct competitor/
precedent I found.

**[LIT] Body/clothing effects, confirmed as a studied factor (not our own speculation):** research
on wearable arrays has *"measured acoustic impulse responses with microphones on the torso covered
by various clothing items including cotton t-shirts, dress shirts, and sweaters... from 24 source
angles to 80 points across the body"* — [Acoustic Impulse Responses for Wearable Audio Devices,
UIUC](https://publish.illinois.edu/corey1/acoustic-impulse-responses-for-wearable-audio-devices/). This confirms torso/clothing measurably shapes the acoustic
field at a worn array, though I don't have the specific magnitude of effect from what I could
access — **[INFER]** given our mics sit at belt height with the body directly behind/between
opposing mics, some torso shadowing between e.g. FRONT and BACK is physically plausible and could
partially explain asymmetric behavior, but this is inference, not measured for our specific device.

## 10. Is physical acoustic shaping worth prototyping?

**Conditionally yes, but with sharply narrowed expectations set by Section 9's frequency math:**
worth a cheap, reversible experiment (Section 17) specifically to see if it helps with
high-frequency/high-pitched sounds, where the physics is actually favorable. **Not** justified as
a general fix for speech/horn/most-alarm direction accuracy — the literature-confirmed frequency
floor on horn/baffle directivity (Section 5-9) makes that expectation physically unreasonable for
a belt-scale structure, not just untested.

## 11-12. Recommended mechanical prototype designs & dimensions [INFER + EXP]

If prototyping: start with **Option B (shallow hood)**, not a horn/cone — the "constant directivity
horn only works down to wavelength≈mouth-size" finding (Section 9) means a *longer* horn buys
almost nothing extra at our achievable size, while adding weight/complexity/reflection risk.
Suggested range to test: **hood depth 1.5-3cm, mouth diameter 3-5cm**, open cardboard/foam-board
cone or 3D-printed shallow bowl, per your own "no giant funnel" constraint. This size range should
start affecting frequencies above roughly 7-11kHz per Section 9's math — test whether that band
matters enough for your actual target sounds before committing further.

## 13. Recommended signal-processing architecture (near-term)

**[INFER, synthesizing 3-9]:** prioritize **Option 3** (current RMS + frequency-band spatial
features) over **Option 4/6** (acoustic structures / ToA) for near-term effort, because:
- The literature-favored method class for our array size (energy/intensity) is what we already
  use — refining it is lower-risk than adding new hardware or algorithms.
- The frequency-band finding (different far-field regimes per band, Section 9) is a concrete,
  calculated reason a band-aware energy comparison could genuinely help, independent of any
  physical structure.
- Physical structures only plausibly help a narrow high-frequency slice; pairwise ToA has real but
  SNR-limited value confirmed by literature, and is more implementation cost than a frequency-band
  feature.

## 14. Are frequency-band spatial features worthwhile?

**Yes, worth prototyping next, ahead of ToA and ahead of a physical structure** — supported by two
independent things: **[LIT]** subband DOA processing is an established technique (32-band
logarithmic filterbank / ERB / Bark-scale grouping approaches exist in the literature), and
**[CALC]** Section 9's far-field boundary table shows low and high bands are in *different physical
regimes* at the same real distance — a single broadband RMS comparison is, by construction,
averaging across bands that behave differently. Computational cost on ESP32-S3: a 2-3 band split
(e.g., simple IIR low/mid/high filters, reusing the same one-pole-filter pattern already used for
the existing high-frequency-ratio feature) is cheap — much cheaper than FFT-based subband
decomposition, and appropriate for a coarse 3-band split rather than the 32-band academic setups.

## 15. Should ToA remain in the system?

It was never implemented (Phase 1 stopped short of it per your instruction). Recommendation:
**keep it planned as low-confidence secondary evidence, implement after frequency-band features,
not before.** Justification: Section 4 shows the underlying delay is real and theoretically
distance-independent, but Section 4/9 also shows measurability is SNR- and near-field-limited in
ways that overlap significantly with the same distance problem we're trying to solve — it's real
evidence, not magic, and shouldn't be the first thing built.

## 16. Ranked solution options

| Option | Expected long-range gain | Complexity | Compute cost | Mechanical risk | Hackathon fit |
|---|---|---|---|---|---|
| **3. RMS + frequency-band features** | Medium-high (targets the actual calculated regime mismatch) | Low-medium | Low | None | **Best fit** |
| 2. RMS + temporal smoothing | Low-medium (Phase 1 will show how much) | Low | Very low | None | Good, already partly done |
| 1. RMS + better calibration | Low | Low | Very low | None | Good, incremental |
| 6. Structures + energy + limited ToA | Medium (narrow band) | High | Medium | Medium (reflections, Section 9) | Risky for remaining time |
| 5. Structures + intensity features | Medium (narrow band) | High | Medium | Medium | Risky |
| 4. RMS + acoustic structures alone | Low-medium (frequency-limited per Section 9) | Medium (mechanical) | Low | Medium | Only for high-pitch case |
| 7. Full TDOA/GCC-PHAT | **Not implementable as specified** — no cross-bus sync | N/A | N/A | N/A | Explicitly ruled out |

**I am not recommending the "likely design direction" hypothesis in the brief (structures + energy
+ smoothing + bands + low-confidence ToA) as a first move** — the research supports doing the
software-only, zero-mechanical-risk items (3, 2, 1) first, since they target the two concretely
measured gaps (SNR collapse partially mitigated by better temporal handling; frequency-regime
mismatch directly addressed by band-splitting), before adding mechanical or ToA complexity whose
benefit is real but narrower (high-frequency-only) than the hypothesis assumed.

## 17. Exact experiment plan (if proceeding to hardware prototyping)

1. Build ONE Option-B shallow hood (cardboard, 2cm deep, 4cm mouth) for one spare mic.
2. Bare vs. hooded, same mic, same position: play a **pure ~8kHz tone** (should show hood effect
   per Section 9) and a **~500Hz tone** (should show ~no hood effect) at 0°/45°/90°/180°, 1m and 3m.
3. Metric: **directional contrast = response(0°) / response(180°)**, compared bare vs. hooded, per
   frequency. If the 8kHz case shows meaningfully higher contrast with the hood and the 500Hz case
   doesn't, that confirms Section 9's prediction rather than assumes it.
4. Only proceed to a 4-mic hooded prototype if step 3 shows a real, repeatable effect.

## 18. Success criteria
Same as Phase 1's: event detection unaffected; close-range direction unaffected; long-range
direction measurably improves on the specific test matrix from Phase 1/2; system says UNKNOWN
rather than confidently wrong when evidence is weak; every claim traceable to either a citation, a
calculation shown here, or a labeled experiment — not a tuned-until-it-looked-good threshold.

## 19. Risks and failure modes
- Frequency-band filtering adds CPU/RAM cost per mic × 4 — needs a compute-budget check, not just
  an assumption it's "cheap enough."
- A physical hood that helps at 8kHz could also introduce a resonance or reflection that hurts a
  currently-working case (e.g. the metal-plate/high-pitch cases already tuned this session) —
  Section 17's bare-vs-hooded comparison is specifically designed to catch this before committing.
- The Fraunhofer/near-field calculations (Section 9) use the idealized point-source, free-field
  formula — real belt-on-body geometry, torso scattering, and clothing (Section 9's UIUC citation)
  will shift these numbers somewhat; treat the boundary table as indicative, not exact.

## 20. References
- [Combined microphone array for precise localization of sound source using the acoustic intensimetry](https://www.sciencedirect.com/science/article/abs/pii/S0888327021002156) — Mechanical Systems and Signal Processing, 2021 (abstract/metadata accessible; full text paywalled)
- [Comparison of the sound source localization methods appropriate for a compact microphone array](https://koreascience.or.kr/article/JAKO202011263332658.page) — Journal of the Acoustical Society of Korea, 2020
- [SmartBelt: A Wearable Microphone Array for Sound Source Localization with Haptic Feedback](https://arxiv.org/pdf/2202.13974) — Michaud, Moffett, Tapia Rousiouk, Duda, Grondin, arXiv:2202.13974, 2022
- [Neural GCC-PHAT for Robust Time-Delay Estimation](https://www.emergentmind.com/topics/neural-generalized-cross-correlation-ngcc-phat)
- [Speaker Localization in Classroom Environments Using GCC-PHAT](http://www.apsipa.org/proceedings/2025/papers/APSIPA2025_P234.pdf) — APSIPA 2025
- [Small Aperture Antenna Arrays for Direction of Arrival Estimation](https://www.ncbi.nlm.nih.gov/pmc/articles/PMC12196855/) (antenna/RF domain, not acoustic)
- [Constant directivity acoustic horn, US Patent 7044265](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/7044265)
- [Fraunhofer and Fresnel Distances: Unified derivation for aperture antennas](https://www.researchgate.net/publication/317394255_Fraunhofer_and_Fresnel_Distances_Unified_derivation_for_aperture_antennas) (antenna domain; formula applied to acoustics is my own extension)
- [Acoustic Impulse Responses for Wearable Audio Devices](https://publish.illinois.edu/corey1/acoustic-impulse-responses-for-wearable-audio-devices/) — Ryan M. Corey, UIUC

**Not fabricated:** every number in Sections 3-4 and 9's tables is my own calculation from stated
formulas and our stated geometry, labeled [CALC]. Every claim labeled [LIT] traces to one of the
sources above; where I could not verify a detail (SmartBelt's exact mic count/algorithm, the
intensimetry paper's full author list), I said so explicitly rather than guessing.
