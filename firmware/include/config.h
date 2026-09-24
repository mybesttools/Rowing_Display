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

// How sensor pulses are interpreted:
//   Stroke     - every pulse is one stroke (counted at the catch). For a
//                sensor that fires once per stroke, or a hand-tapped switch.
//   Revolution - every pulse is one magnet pass on the flywheel; strokes
//                are detected from flywheel speed-up/slow-down (physics
//                model, see metrics_model.h).
enum class PulseMode { Revolution, Stroke };
constexpr PulseMode PULSE_MODE = PulseMode::Stroke;

// Stroke mode tuning. Pulses closer than MIN_STROKE_INTERVAL_MS are switch
// bounce, not strokes (250 ms = 240 spm, far above any real rate).
constexpr uint32_t MIN_STROKE_INTERVAL_MS = 250;
constexpr float METERS_PER_STROKE = 10.0f;
// Fastest plausible boat speed (~1:23 /500m). Rapid tapping would otherwise
// turn a fixed metres-per-stroke into absurd split/watts figures.
constexpr float MAX_BOAT_SPEED_MPS = 6.0f;
// Share of the stroke cycle spent on the drive (rest is recovery), and the
// cycle assumed for the very first stroke before an interval is measured.
constexpr float DRIVE_FRACTION = 0.4f;
constexpr uint32_t FIRST_STROKE_INTERVAL_MS = 2000;

// Smoothing and cadence behavior tuning
constexpr float OMEGA_ALPHA = 0.2f;
constexpr uint32_t STROKE_IDLE_TIMEOUT_MS = 2500;

// Debounce: reject Hall pulses closer together than this, since it implies a
// physically implausible flywheel speed (~1500 rpm ceiling at 1 magnet/rev).
// Without this, comparator chatter / contact bounce on a single real edge
// gets read as several near-instantaneous revolutions and spikes rpm/watts.
constexpr uint32_t MIN_PULSE_INTERVAL_US = 40000;

// Input debounce (main.cpp ISR): a falling edge only counts as a pulse if
// the line had no edges for this long before it. Rejects reed/switch
// chatter on both press and release; well below the ~30 ms the line stays
// high between magnet passes even at top flywheel speed.
constexpr uint32_t PULSE_QUIET_US = 15000;

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
