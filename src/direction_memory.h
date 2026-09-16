#pragma once
#include "spatial_detector.h"

// direction_memory module: tracks onsets (NOTICE/EVENT announcements) per
// direction over time, separate from temporal_reasoner's generic
// any-direction repeat count. Answers two priority-relevant questions a
// deaf user actually cares about:
//   1. Is this the SAME thing recurring from the SAME spot (not just "stuff
//      keeps happening somewhere")?
//   2. Is it getting LOUDER each time from that spot (approaching), or
//      quieter (receding), independent of how loud it is right now?
struct DirectionMemoryResult {
    int  sameDirectionRepeatCount;
    bool isApproaching;
    bool isReceding;
    // True once this direction's NOTICE-level onsets have recurred
    // HABITUATION_STREAK_THRESHOLD+ times in a row without escalating to
    // CANDIDATE/EVENT or trending louder -- caller should suppress
    // re-announcing (LED/serial/BLE eventSeq) but NOT re-detecting, until
    // this direction changes, escalates, or goes quiet for a while. Only
    // meaningful for NOTICE-tier onsets -- always false otherwise, and
    // CANDIDATE/EVENT onsets are never suppressible regardless of this flag.
    bool habituated;
};

void directionMemoryInit();

// Call every frame. Only frames where isOnset is true (a NOTICE or EVENT
// announcement just fired) actually update the history; other frames just
// return the last computed result unchanged. isNoticeTier should be
// temporal.noticeAnnounce -- distinguishes a NOTICE onset (habituation-
// eligible) from an EVENT onset (never suppressed, always resets the streak).
DirectionMemoryResult directionMemoryUpdate(Direction direction, float eventScore, bool isOnset, bool isNoticeTier);
