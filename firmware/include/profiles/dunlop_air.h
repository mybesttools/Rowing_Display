#pragma once

// Rower profile: Dunlop Air Rower 750 (fan/flywheel, air resistance).
//
// Machine-specific physics calibration constants only — board/sensor
// config lives in config.h, physics derivation lives in metrics_model.h,
// full "inspired by" attribution is in Hardware.md's "Metrics/physics
// model" section.

#include <cstdint>

namespace cfg {

constexpr uint8_t MAGNETS_PER_REV = 1;

// UNCALIBRATED placeholder for this specific fan flywheel — needs a real
// measurement (bifilar pendulum swing test) or iterative tuning once real
// rowing numbers are on screen. See Hardware.md open items.
constexpr float FLYWHEEL_INERTIA_KGM2 = 0.10f;

// Seed drag factor (k, kg*m^2) used before the first recovery phase has
// produced a fitted estimate. Chosen so P=k*omega^3 and v=(k/C)^(1/3)*omega
// land in a plausible range (~120W, ~3.2m/s) at a moderate flywheel speed
// (~15 rad/s, ~140 rpm).
constexpr float INITIAL_DRAG_FACTOR = 0.035f;

// Plausibility band for an accepted drag-factor fit — wide (roughly two
// orders of magnitude either side) since we don't yet know the real value;
// this only exists to reject obviously-broken regressions, not to
// second-guess a real fit once calibrated.
constexpr float DRAG_FACTOR_MIN = 0.0005f;
constexpr float DRAG_FACTOR_MAX = 1.0f;

// How much a newly-accepted per-stroke drag-factor fit moves the running
// (displayed) value. Drag is physically near-constant stroke to stroke, so
// this is deliberately slow.
constexpr float DRAG_FACTOR_ALPHA = 0.25f;

// Boat-scaling constant C in v = (k/C)^(1/3) * omega. Empirical per-machine
// constant — needs tuning against known-distance test pieces.
constexpr float BOAT_SCALING_CONSTANT = 3.7f;

// Phase-transition debounce: air resistance flywheels spin down relatively
// quickly, so recovery is comparatively short.
constexpr uint32_t MIN_DRIVE_TIME_MS = 200;
constexpr uint32_t MIN_RECOVERY_TIME_MS = 400;

} // namespace cfg
