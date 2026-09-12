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
};

void directionMemoryInit();

// Call every frame. Only frames where isOnset is true (a NOTICE or EVENT
// announcement just fired) actually update the history; other frames just
// return the last computed result unchanged.
DirectionMemoryResult directionMemoryUpdate(Direction direction, float eventScore, bool isOnset);
