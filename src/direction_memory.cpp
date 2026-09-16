#include "direction_memory.h"
#include "config.h"
#include <string.h>

struct Entry {
    unsigned long t;
    Direction dir;
    float score;
};

static Entry history[DIRECTION_HISTORY_SIZE];
static int historyCount = 0;
static unsigned long frameCounter = 0;
static DirectionMemoryResult cached = {0, false, false, false};

// Habituation state -- deliberately separate from `history` above, which is
// a short (DIRECTION_HISTORY_SIZE-slot, ~REPEAT_WINDOW_FRAMES-wide) ring
// buffer for the sameDirectionRepeatCount/approaching calc. Habituation
// needs to persist across a much longer timescale (many bursts over
// minutes), so it's just a running streak count, not a windowed history.
static Direction lastNoticeDir = Direction::UNKNOWN;
static unsigned long lastNoticeFrame = 0;
static int habituationStreak = 0;

void directionMemoryInit() {
    historyCount = 0;
    frameCounter = 0;
    cached = {0, false, false, false};
    lastNoticeDir = Direction::UNKNOWN;
    lastNoticeFrame = 0;
    habituationStreak = 0;
}

// Two directions count as "the same source" if they're identical, or one is
// a merged label containing the other (e.g. FRONT and FRONT-RIGHT) -- a
// source near a boundary can wobble between a single and merged label
// frame to frame without actually being a different physical direction.
// OMNIDIRECTIONAL/UNKNOWN never match anything (too ambiguous to attribute).
static bool directionsMatch(Direction a, Direction b) {
    if (a == Direction::UNKNOWN || b == Direction::UNKNOWN) return false;
    if (a == Direction::OMNIDIRECTIONAL || b == Direction::OMNIDIRECTIONAL) return false;
    if (a == b) return true;
    const char *na = directionName(a);
    const char *nb = directionName(b);
    return (strstr(na, nb) != nullptr) || (strstr(nb, na) != nullptr);
}

DirectionMemoryResult directionMemoryUpdate(Direction direction, float eventScore, bool isOnset, bool isNoticeTier) {
    frameCounter++;
    if (!isOnset || direction == Direction::UNKNOWN) {
        return cached;
    }

    if (historyCount < DIRECTION_HISTORY_SIZE) {
        history[historyCount++] = {frameCounter, direction, eventScore};
    } else {
        for (int i = 1; i < DIRECTION_HISTORY_SIZE; i++) history[i - 1] = history[i];
        history[DIRECTION_HISTORY_SIZE - 1] = {frameCounter, direction, eventScore};
    }

    int count = 0;
    float scores[DIRECTION_HISTORY_SIZE];
    int scoreCount = 0;
    for (int i = 0; i < historyCount; i++) {
        if (frameCounter - history[i].t <= (unsigned long)REPEAT_WINDOW_FRAMES &&
            directionsMatch(history[i].dir, direction)) {
            count++;
            scores[scoreCount++] = history[i].score;
        }
    }

    bool approaching = false;
    bool receding = false;
    if (scoreCount >= 3) {
        int half = scoreCount / 2;
        float earlyAvg = 0.0f, lateAvg = 0.0f;
        for (int i = 0; i < half; i++) earlyAvg += scores[i];
        earlyAvg /= half;
        for (int i = half; i < scoreCount; i++) lateAvg += scores[i];
        lateAvg /= (scoreCount - half);

        float change = (lateAvg - earlyAvg) / (earlyAvg > 0.01f ? earlyAvg : 0.01f);
        if (change >= APPROACHING_TREND_THRESHOLD) approaching = true;
        else if (change <= -APPROACHING_TREND_THRESHOLD) receding = true;
    }

    // ---- Habituation: NOTICE-tier onsets only. CANDIDATE/EVENT onsets
    // always reset the streak (a real escalation, not background noise) but
    // are never themselves suppressible -- only a NOTICE announce should
    // ever be gated by `habituated`.
    if (isNoticeTier) {
        bool sameAsLastNotice = directionsMatch(lastNoticeDir, direction);
        bool quietGapExpired = (frameCounter - lastNoticeFrame) > (unsigned long)HABITUATION_RESET_FRAMES;
        if (sameAsLastNotice && !quietGapExpired && !approaching) {
            habituationStreak++;
        } else {
            habituationStreak = 0;
        }
        lastNoticeDir = direction;
        lastNoticeFrame = frameCounter;
        cached.habituated = habituationStreak >= HABITUATION_STREAK_THRESHOLD;
    } else {
        habituationStreak = 0;
        cached.habituated = false;
    }

    cached.sameDirectionRepeatCount = count;
    cached.isApproaching = approaching;
    cached.isReceding = receding;
    return cached;
}
