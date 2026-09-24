#pragma once

#include <cstdint>

// Rower profile selection: define ROWER_PROFILE_KUIPERS_AQUAROWER (via
// build_flags) to target the Kuipers AquaRower; defaults to the Dunlop Air
// Rower otherwise. Profiles hold the machine-specific physics calibration
// constants (flywheel inertia, drag factor range, boat-scaling constant,
// phase-detection timing) — see firmware/include/profiles/ and Hardware.md
// "Metrics/physics model". Board/sensor config below is independent of
// which machine this ESP32 is mounted on.
//
// This board-profile/rower-profile split is a pattern mirrored from
// ESPRowingMonitor's architecture (see Hardware.md attribution) — that
// project ships distinct profiles per rowing machine for exactly this
// reason: physical calibration shouldn't be entangled with pin mappings.
#if defined(ROWER_PROFILE_KUIPERS_AQUAROWER)
#include "profiles/kuipers_aquarower.h"
#else
#include "profiles/dunlop_air.h"
#endif

namespace cfg {

// GPIO6 is the one pin this board's own examples (04_Sensor_AD) expose for a
// general external sensor. Every other low GPIO is already claimed onboard:
// LCD data/timing (0,1,2,3,5,7,10,14,17,18,21,38-48), touch+expander I2C
// (8,9,4), SD card SPI (11,12,13), RS485 (15,16), and native USB/TWAI (19,20).
// GPIO16 in particular is the RS485 transceiver's TX line — do not use it here.
constexpr uint8_t HALL_PIN = 6;

constexpr uint32_t SERIAL_BAUD = 115200;

// Smoothing and cadence behavior tuning
constexpr float OMEGA_ALPHA = 0.2f;
constexpr uint32_t STROKE_IDLE_TIMEOUT_MS = 2500;

// Debounce: reject Hall pulses closer together than this, since it implies a
// physically implausible flywheel speed (~1500 rpm ceiling at 1 magnet/rev).
// Without this, comparator chatter / contact bounce on a single real edge
// gets read as several near-instantaneous revolutions and spikes rpm/watts.
constexpr uint32_t MIN_PULSE_INTERVAL_US = 40000;

// Metrics update cadence
constexpr uint32_t LOOP_INTERVAL_MS = 50;
constexpr uint32_t FTMS_NOTIFY_INTERVAL_MS = 200;

// Recovery-phase regression: need at least this many (time, 1/omega) points
// to attempt a fit, and the fit's R^2 must clear this bar to be accepted.
// General algorithm robustness tuning, not machine-specific physics, so
// it's shared across rower profiles.
constexpr int MIN_REGRESSION_POINTS = 4;
constexpr float MIN_FIT_R2 = 0.7f;

} // namespace cfg
