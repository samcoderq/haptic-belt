# What Existing Products/Research Do, and What This Project Should Do Next

## 1. What's already out there

| Product/System | Approach | Relevant takeaway |
|---|---|---|
| **[Neosensory Buzz](https://hearinghealthmatters.org/hearing-technologies/2020/feeling-sound-as-vibration-a-look-at-the-neosensory-buzz/)** (wrist-worn) | Splits sound into 8 frequency bands, maps to 4 motors by pitch (not direction) | Closest commercial analog to "feel sound," but encodes **pitch**, not **location** — no direction awareness at all |
| **[Apple Sound Recognition](https://techcrunch.com/2020/06/25/ios-14-lets-deaf-users-set-alerts-for-important-sounds-among-other-clever-accessibility-perks)** (iPhone/Watch) | On-device classifier, fixed sound list (siren, smoke alarm, dog bark, doorbell, car horn, running water, appliance beep) | Tells you **what**, never **where**. Single mic (phone), no spatial info at all |
| **[Google Sound Notifications](https://support.google.com/accessibility/android/answer/10092548)** (Android, built with Gallaudet University) | On-device, fully offline, vibration/flash/push notification | Same limitation — what, not where. Notably: fully offline by design (privacy), matches our own on-device approach |
| **[SafeAwake / Bellman](https://us.bellman.com/blogs/news/smoke-alarm-for-deaf-people-visual-vs-vibrating-options)** bed-shaker fire alarm systems | Dedicated bed-shaker + strobe, separate from any wearable | **Important finding:** neither a worn device nor a room-flash reliably wakes someone from deep sleep — only a bed/pillow-contact shaker does |
| **[SoundWatch](https://dl.acm.org/doi/fullHtml/10.1145/3597638.3608431)** (academic, smartwatch) | On-device classifier, 3-week real-world field study with 10 DHH users | First real longitudinal field study of this kind — see findings below |

**The gap all of them share: none give real-time direction.** A CHI paper studying DHH preferences found *"the value of visualizing localization was consistent across participants"* — direction is something users explicitly want and current commercial products don't provide. That's this project's actual point of novelty, not a side feature.

## 2. What real-world testing of these systems found (the part that should worry us)

The SoundWatch field study (["Not There Yet", ACM ASSETS 2023](https://dl.acm.org/doi/fullHtml/10.1145/3597638.3608431)) is the most directly relevant data point — real DHH users, real homes, three weeks:

- **The app fired ~112.7 sound events per day per user.** Participants paid attention to every notification early on; by the end of the study, several had started ignoring them. That's alert fatigue, measured, not theoretical.
- **The dominant error was false positives**, disproportionately from acoustically-similar-but-wrong classes (birds vs. jacket rustling was a specifically named recurring confusion).
- Researchers' own recommendation: **end-user customization and contextual awareness** — not a single fixed sensitivity for all situations.

**This is directly relevant to what we've been doing this session.** Every tuning pass today has pushed toward *more* sensitive, *lower* thresholds, favoring recall over precision — reasonable per-change, but the research says that path has a real ceiling: past a certain point, more alerts measurably means less attention paid to any of them. We haven't hit that ceiling, but we should track it as a known tradeoff, not an unlimited dial.

## 3. Haptic design specifics (for when motors get built)

- **Perceived urgency correlates with shorter burst duration** — short sharp pulses read as more urgent, not longer ones. Worth keeping in mind for whatever haptic language design comes next; it's a concrete, testable rule rather than a guess.
- **Location-based haptic alerts significantly outperformed pattern-based alerts** in hazard detection rate and reaction time in direct comparison studies. This validates prioritizing direction *accuracy* over an elaborate vibration-pattern vocabulary — getting the right motor to buzz matters more than how cleverly it buzzes.

## 4. Concrete recommendations for this project, in priority order

1. **Add a habituation/suppression mechanism for sustained-but-already-acknowledged situations.** ~~Right now a continuous alarm re-announces on its own cooldown timer forever.~~ **First-pass implementation done** (`direction_memory.h/.cpp`'s `habituated` field + `HABITUATION_STREAK_THRESHOLD`/`HABITUATION_RESET_FRAMES` in `config.h`): after `HABITUATION_STREAK_THRESHOLD` (6) consecutive non-escalating, non-approaching NOTICE-tier onsets from the same direction, further NOTICE announcements (LED flash, serial log, BLE eventSeq bump) are suppressed until the direction changes, escalates to CANDIDATE/EVENT, or goes quiet for `HABITUATION_RESET_FRAMES` (~60s). CANDIDATE/EVENT-tier onsets are never suppressed. Untuned first-pass values (no real data to tune against), and **completely untested against real hardware** — compiles clean, never run.
2. **User-adjustable sensitivity / operating modes** (Home / Outdoor / Sleep) — already on the original roadmap, now backed by the field study's explicit recommendation. Not yet built.
3. **Be honest that the belt is a daytime/worn-device solution, not a sleep-safety device.** The bed-shaker research is clear that nothing worn or room-mounted reliably wakes someone from deep sleep — only pillow/mattress contact does. We should not imply the belt covers nighttime emergencies; that's a solved, separate problem (bed shakers exist and work) and shouldn't be reinvented or implied as covered.
4. **When real classification eventually gets built, validate specifically against known confusion classes** (the field study named birds vs. fabric rustling) rather than assuming a generic model handles this — this is a documented, specific failure mode, not a hypothetical one.
5. **A lightweight "was that right?" feedback mechanism** (even just a belt button) — the AdaptiveSound research found that giving DHH users a way to correct the system built trust and a sense of agency, beyond just accuracy gains. Matches the original plan's "personalized sound recognition" idea.

## 5. What NOT to do
Don't chase pitch-based haptic encoding (Neosensory's approach) — it doesn't map to this project's actual value proposition (direction), and mixing two different vibration semantics (pitch and location) on the same 4 motors would confuse both. Direction, priority, and urgency are the right things to encode; frequency is not.
