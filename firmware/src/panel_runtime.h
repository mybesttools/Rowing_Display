#pragma once

#include "metrics_model.h"

namespace panel_runtime {

bool begin();
void update(const RowerMetrics& metrics);

} // namespace panel_runtime
