#pragma once

// temporal_reasoner module: owns the awareness state machine
// (BACKGROUND -> NOTICE/CANDIDATE -> EVENT -> COOLDOWN -> BACKGROUND),
// hysteresis, persistence, and repetition tracking. It only ever sees
// eventScore -- it has no idea what produced it.
enum class AwarenessState { BACKGROUND, NOTICE, CANDIDATE, EVENT, COOLDOWN };

struct TemporalResult {
    AwarenessState state;
    bool noticeAnnounce;    // print the one-line NOTICE announcement this frame
    bool eventAnnounce;     // print the EVENT DETECTED block this frame
    bool repeatAnnounce;    // print REPEATED ACTIVITY this frame
    int  repeatCount;       // onsets within the trailing repeat window
    const char* pattern;    // "PERIODIC" / "IRREGULAR" / "-"
    int  eventPersistFrames;
    int  noticePersistFrames;  // how long we've continuously been in NOTICE (0 if not in NOTICE)
};

void temporalReasonerInit();

// Query BEFORE calling temporalReasonerUpdate() this frame -- reflects the
// state as of the END of the previous frame, which is what environment_model
// needs to decide whether to freeze the baseline for the frame about to be
// processed.
bool temporalReasonerIsActive();

TemporalResult temporalReasonerUpdate(float eventScore);
const char* awarenessStateName(AwarenessState s);
