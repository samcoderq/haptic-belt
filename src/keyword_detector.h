#pragma once

// keyword_detector module: continuously listens for an enrolled keyword/
// name (keyword_store.h) using energy-based speech endpointing + MFCC/DTW
// matching -- independent of the amplitude-based event/temporal state
// machine, since a spoken name may not be loud enough to cross NOTICE/
// CANDIDATE thresholds the way a bang or alarm would.
//
// Rewritten (2026-09-15) from the original Goertzel-power-ratio-bins +
// fixed-7-segment-window + Euclidean distance approach: a controlled
// real-hardware test measured pure silence and continuous real speech of an
// enrolled word SEPARATELY (the first tuning attempt blended them together
// in one capture, which overstated how separable they looked) and found
// their distances almost entirely overlapped (silence 0.78-1.34, speech
// 0.95-1.78) -- no threshold could ever cleanly tell them apart. This
// rewrite fixes the two root causes that diagnostic pointed at: (1) the
// matcher used to run unconditionally every check, on whatever was in a
// blind rolling window, silence included -- now it only ever attempts a
// match once real speech has been detected and has ended (energy-based
// endpointing, mirroring the onset detection KeywordFeatures.kt already did
// phone-side); (2) fixed-position comparison couldn't tolerate any timing
// misalignment between the enrolled template and a live utterance -- MFCC
// (mfcc_dsp.h) + DTW (dtw.h) replace it, time-aligning the two sequences
// instead of comparing frame-for-frame at fixed positions. MFCC borrows the
// feature-extraction idea (not the trained-CNN classifier -- no dataset for
// that here) from https://github.com/lbalic21/keyword_spotting; MFCC+DTW
// together is the standard technique for few-shot custom keyword spotting
// with no training pipeline, per current research.
struct KeywordMatch {
    bool matched;
    const char* name;
    int index;      // keyword_store slot index (0-based); only meaningful if matched
    float distance; // normalized DTW cost, lower = better match; only meaningful if matched
};

void keywordDetectorInit();

// Call every main loop frame with the current frame's leading mic index --
// feeds this frame's raw samples into the streaming MFCC frame extractor
// and speech endpointer. matched is true only on the frame a just-ended
// utterance was compared against every enrolled keyword and the best match
// cleared MAX_MATCH_DISTANCE.
KeywordMatch keywordDetectorUpdate(int leadingMic);
