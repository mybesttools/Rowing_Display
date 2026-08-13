#pragma once

#include <cstddef>
#include <cstdint>

#include "metrics_model.h"

namespace ftms {

// Encodes a minimal Indoor Rower Data (UUID 0x2AD1) packet.
// Returns packet size in bytes.
size_t encodeIndoorRowerData(const RowerMetrics& m, uint8_t* out, size_t cap);

} // namespace ftms
