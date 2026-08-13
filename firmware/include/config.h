#pragma once

#include <cstdint>

namespace cfg {

// Do not use pins consumed by RGB data/timing lines on the 7-inch board.
// Verify final choice against your exact board revision before wiring permanently.
constexpr uint8_t HALL_PIN = 16;
constexpr uint8_t MAGNETS_PER_REV = 1;

constexpr uint32_t SERIAL_BAUD = 115200;

// Smoothing and cadence behavior tuning
constexpr float RPM_ALPHA = 0.2f;
constexpr uint32_t STROKE_IDLE_TIMEOUT_MS = 2500;

// Metrics update cadence
constexpr uint32_t LOOP_INTERVAL_MS = 50;
constexpr uint32_t FTMS_NOTIFY_INTERVAL_MS = 200;

// Very rough starting factor for distance integration.
// Tune this after hardware calibration.
constexpr float METERS_PER_REV = 0.12f;

} // namespace cfg
