#pragma once

#include "metrics_model.h"

namespace panel_runtime {

bool begin();
void update(const RowerMetrics& metrics);

// True exactly once, the loop iteration after the user pressed a "Start
// Session" button on the home screen - the caller should reset the metrics
// model at that point so the new session starts from zero.
bool consumeSessionStartRequested();

} // namespace panel_runtime
