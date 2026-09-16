#include "temporal_reasoner.h"
#include "config.h"
#include "runtime_config.h"
#include <math.h>

static AwarenessState state = AwarenessState::BACKGROUND;
static int candidateFrames = 0;
static int cooldownFrames = 0;
static int noticeCooldown = 0;
static int eventPersistFrames = 0;
static int noticeSustainCounter = 0;  // consecutive weak-band frames while still in BACKGROUND
static int noticePersistFrames = 0;   // consecutive frames actually spent in NOTICE

static unsigned long onsetHistory[REPEAT_HISTORY_SIZE];
static int onsetHistoryCount = 0;
static unsigned long frameCounter = 0;

void temporalReasonerInit() {
    state = AwarenessState::BACKGROUND;
    candidateFrames = 0;
    cooldownFrames = 0;
    noticeCooldown = 0;
    eventPersistFrames = 0;
    noticeSustainCounter = 0;
    noticePersistFrames = 0;
    onsetHistoryCount = 0;
    frameCounter = 0;
}

bool temporalReasonerIsActive() {
    return state == AwarenessState::CANDIDATE || state == AwarenessState::EVENT;
}

const char* awarenessStateName(AwarenessState s) {
    switch (s) {
        case AwarenessState::BACKGROUND: return "BACKGROUND";
        case AwarenessState::NOTICE:     return "NOTICE";
        case AwarenessState::CANDIDATE:  return "CANDIDATE";
        case AwarenessState::EVENT:      return "EVENT";
        case AwarenessState::COOLDOWN:   return "COOLDOWN";
    }
    return "?";
}

static void recordOnset(unsigned long t) {
    if (onsetHistoryCount < REPEAT_HISTORY_SIZE) {
        onsetHistory[onsetHistoryCount++] = t;
    } else {
        for (int i = 1; i < REPEAT_HISTORY_SIZE; i++) onsetHistory[i - 1] = onsetHistory[i];
        onsetHistory[REPEAT_HISTORY_SIZE - 1] = t;
    }
}

// Counts onsets within the trailing repeat window and, if that count crosses
// the threshold, classifies the spacing as PERIODIC or IRREGULAR using the
// coefficient of variation of inter-onset intervals -- cheap and explainable,
// not a claim of a validated rhythm-detection algorithm.
static void evaluateRepetition(unsigned long now, TemporalResult &out) {
    int count = 0;
    unsigned long intervals[REPEAT_HISTORY_SIZE];
    int intervalCount = 0;
    unsigned long lastT = 0;
    bool haveLast = false;

    for (int i = 0; i < onsetHistoryCount; i++) {
        if (now - onsetHistory[i] <= (unsigned long)REPEAT_WINDOW_FRAMES) {
            count++;
            if (haveLast) {
                intervals[intervalCount++] = onsetHistory[i] - lastT;
            }
            lastT = onsetHistory[i];
            haveLast = true;
        }
    }

    out.repeatCount = count;
    out.pattern = "-";
    out.repeatAnnounce = false;

    if (count >= REPEAT_COUNT_THRESHOLD) {
        out.repeatAnnounce = true;
        if (intervalCount >= 2) {
            float mean = 0.0f;
            for (int i = 0; i < intervalCount; i++) mean += (float)intervals[i];
            mean /= intervalCount;
            float variance = 0.0f;
            for (int i = 0; i < intervalCount; i++) {
                float d = (float)intervals[i] - mean;
                variance += d * d;
            }
            variance /= intervalCount;
            float cv = mean > 0.0f ? sqrtf(variance) / mean : 1.0f;
            out.pattern = (cv < 0.4f) ? "PERIODIC" : "IRREGULAR";
        } else {
            out.pattern = "IRREGULAR";
        }
    }
}

static void enterEvent(unsigned long now, TemporalResult &out) {
    state = AwarenessState::EVENT;
    out.eventAnnounce = true;
    eventPersistFrames = 1;
    recordOnset(now);
    evaluateRepetition(now, out);
}

TemporalResult temporalReasonerUpdate(float eventScore) {
    frameCounter++;
    unsigned long now = frameCounter;

    TemporalResult out;
    out.noticeAnnounce = false;
    out.eventAnnounce = false;
    out.repeatAnnounce = false;
    out.repeatCount = 0;
    out.pattern = "-";

    if (noticeCooldown > 0) noticeCooldown--;

    switch (state) {
        case AwarenessState::BACKGROUND:
            eventPersistFrames = 0;
            if (eventScore >= g_instantThreshold) {
                enterEvent(now, out);
                noticeSustainCounter = 0;
            } else if (eventScore >= g_candidateThreshold) {
                state = AwarenessState::CANDIDATE;
                candidateFrames = 1;
                noticeSustainCounter = 0;
            } else if (eventScore >= g_noticeEnterThreshold) {
                // Weak band: require it to actually sustain before it's worth
                // surfacing at all -- a single brief nearby blip should never
                // reach the user, only something that keeps going.
                noticeSustainCounter++;
                if (noticeSustainCounter >= NOTICE_SUSTAIN_FRAMES) {
                    state = AwarenessState::NOTICE;
                    noticePersistFrames = noticeSustainCounter;
                    if (noticeCooldown == 0) {
                        out.noticeAnnounce = true;
                        noticeCooldown = NOTICE_COOLDOWN_FRAMES;
                        recordOnset(now);
                        evaluateRepetition(now, out);
                    }
                }
            } else {
                noticeSustainCounter = 0;
            }
            break;

        case AwarenessState::NOTICE:
            noticePersistFrames++;
            if (eventScore >= g_instantThreshold) {
                enterEvent(now, out);
                noticePersistFrames = 0;
            } else if (eventScore >= g_candidateThreshold) {
                state = AwarenessState::CANDIDATE;
                candidateFrames = 1;
                noticePersistFrames = 0;
            } else if (eventScore < g_noticeExitThreshold) {
                state = AwarenessState::BACKGROUND;
                noticeSustainCounter = 0;
                noticePersistFrames = 0;
            } else if (noticeCooldown == 0) {
                // Still going -- re-announce periodically so priority can
                // reflect accumulated persistence/repetition, not just the
                // low persistence seen at the very first announcement. This
                // is what lets a sustained-but-quiet sound (e.g. a distant
                // siren) escalate over time instead of being reported once
                // at LOW and then going silent for as long as it continues.
                out.noticeAnnounce = true;
                noticeCooldown = NOTICE_COOLDOWN_FRAMES;
                evaluateRepetition(now, out);
            }
            break;

        case AwarenessState::CANDIDATE:
            if (eventScore >= g_instantThreshold) {
                enterEvent(now, out);
            } else if (eventScore >= g_candidateThreshold) {
                candidateFrames++;
                if (candidateFrames >= CONFIRM_FRAMES) {
                    enterEvent(now, out);
                }
            } else if (eventScore >= g_noticeEnterThreshold) {
                // Already proved itself substantial (crossed 0.5 at least once) --
                // no extra sustain gate needed, unlike a fresh weak-band signal.
                state = AwarenessState::NOTICE;
                noticePersistFrames = 1;
                candidateFrames = 0;
                if (noticeCooldown == 0) {
                    out.noticeAnnounce = true;
                    noticeCooldown = NOTICE_COOLDOWN_FRAMES;
                    recordOnset(now);
                    evaluateRepetition(now, out);
                }
            } else {
                state = AwarenessState::BACKGROUND;
                candidateFrames = 0;
            }
            break;

        case AwarenessState::EVENT:
            eventPersistFrames++;
            noticePersistFrames = 0;
            if (eventScore < g_deactivateThreshold) {
                state = AwarenessState::COOLDOWN;
                cooldownFrames = COOLDOWN_FRAMES;
            }
            break;

        case AwarenessState::COOLDOWN:
            cooldownFrames--;
            noticePersistFrames = 0;
            if (cooldownFrames <= 0) {
                state = AwarenessState::BACKGROUND;
                candidateFrames = 0;
            }
            break;
    }

    out.state = state;
    out.eventPersistFrames = eventPersistFrames;
    out.noticePersistFrames = noticePersistFrames;
    return out;
}
