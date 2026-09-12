#include "classifier_interface.h"

ClassificationResult classifyEvent(const AudioFrame &frame, const EventEvaluation &eval) {
    (void)frame;
    (void)eval;
    return ClassificationResult{"UNKNOWN", 0.0f};
}
