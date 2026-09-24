#pragma once

// Rower profile: Kuipers AquaRower (water resistance, two-fin paddle wheel
// in an adjustable 12-21L tank).
//
// UNCALIBRATED: nobody has measured this specific unit yet — these are
// order-of-magnitude placeholders reasoned from the machine being a
// water-filled paddle drum (far more rotating mass than the Dunlop's bare
// fan blades), not from any real measurement. The same P=k*omega^3 /
// recovery-phase drag regression physics applies (fluid drag is
// quadratic in velocity for both air and water in the turbulent regime
// the flywheel operates in — see metrics_model.h) — only the constants
// differ. Treat every value here as a starting point to replace once the
// machine is instrumented and calibrated, same as the Dunlop profile.

#include <cstdint>

namespace cfg {

// A real DIY build on a similar water-tank paddle-wheel rower
// (rsboni/rowing_cpu, no LICENSE — cited for this data point only, no code
// reused) used 5 magnets on the drive wheel rather than 1, for finer
// angular resolution on a machine that likely spins slower than the
// Dunlop's air fan. Starting from that instead of guessing 1.
// Note: this also lowers the effective max-RPM ceiling implied by
// MIN_PULSE_INTERVAL_US in config.h (scales down by the same factor as
// MAGNETS_PER_REV goes up) — revisit that constant if this profile's real
// flywheel speed ever gets clipped.
constexpr uint8_t MAGNETS_PER_REV = 5;

// A water-filled paddle drum (a few kg of water plus drum/fins/shaft) is
// an order of magnitude more rotating mass than the Dunlop's bare fan.
constexpr float FLYWHEEL_INERTIA_KGM2 = 1.0f;

// Water drag is characteristically much higher than air drag at comparable
// speeds, hence the larger seed/bounds versus the air profile.
constexpr float INITIAL_DRAG_FACTOR = 0.35f;
constexpr float DRAG_FACTOR_MIN = 0.01f;
constexpr float DRAG_FACTOR_MAX = 5.0f;

constexpr float DRAG_FACTOR_ALPHA = 0.25f;

// Boat-scaling constant C in v = (k/C)^(1/3) * omega. Empirical per-machine
// constant — needs tuning against known-distance test pieces once the
// machine is instrumented.
constexpr float BOAT_SCALING_CONSTANT = 3.7f;

// Phase-transition debounce: water resistance recovery tends to run longer
// (water still dissipating momentum) than an air fan's quick spin-down.
constexpr uint32_t MIN_DRIVE_TIME_MS = 250;
constexpr uint32_t MIN_RECOVERY_TIME_MS = 600;

} // namespace cfg
