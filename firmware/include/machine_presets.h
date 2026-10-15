#pragma once

// Factory defaults per rowing machine and per sensor type. These are what
// the on-device Settings screen starts from and resets to; the user's
// overrides are stored in NVS (settings.cpp). Pure data, so the native unit
// tests can use it too.

#include <cstdint>

namespace cfg {

// How sensor pulses are interpreted:
//   Revolution - every pulse is one magnet pass on the flywheel; strokes
//                are detected from flywheel speed-up/slow-down.
//   Stroke     - every pulse is one stroke (a sensor that fires once per
//                stroke, or a hand-tapped test switch).
enum class PulseMode : uint8_t { Revolution, Stroke };

enum class MachineType : uint8_t { KuipersAquaRower, DunlopAir };
enum class SensorType : uint8_t { Reed, Hall };

struct MachineParams {
    uint8_t pulsesPerRev;      // magnets on the wheel/fan
    float flywheelInertia;     // kg*m^2; scales the fitted drag factor
    float initialDragFactor;   // k used until the first recovery fit
    float dragFactorMin;       // plausibility band for a fit (not in UI)
    float dragFactorMax;
    float boatConstant;        // C in v = (k/C)^(1/3) * omega
    uint16_t minDriveMs;       // phase must hold this long before flipping
    uint16_t minRecoveryMs;
    uint16_t minPulseGapMs;    // pulses closer than this are rejected
};

struct SensorParams {
    uint16_t debounceMs; // line must be quiet this long before an edge counts
};

// Kuipers AquaRower (water paddle wheel). Calibrated 2026-10-15 against the
// original Kuipers monitor (354 m in 2:00 = 2.95 m/s) from a logged session
// at the same rate: 1 pulse per wheel turn, 1.33 m per turn. C = 2.80 makes
// watts follow Concept2's P = 2.80*v^3; inertia 1.9 turns the fitted
// recovery slope (~0.014) into k ~= 0.026. Not a measured inertia.
constexpr MachineParams kKuipersAquaRower{
    1, 1.9f, 0.026f, 0.01f, 5.0f, 2.80f, 250, 600, 40,
};

// Dunlop Air Rower 750 (air fan). UNCALIBRATED: inertia and C are
// placeholders until compared against a known distance.
constexpr MachineParams kDunlopAir{
    1, 0.10f, 0.035f, 0.0005f, 1.0f, 3.7f, 200, 400, 40,
};

// Reed contacts bounce for a few ms on close and open; Hall sensors switch
// cleanly.
constexpr SensorParams kReedSensor{15};
constexpr SensorParams kHallSensor{2};

constexpr const MachineParams& machinePreset(MachineType t) {
    return t == MachineType::DunlopAir ? kDunlopAir : kKuipersAquaRower;
}

constexpr const SensorParams& sensorPreset(SensorType t) {
    return t == SensorType::Hall ? kHallSensor : kReedSensor;
}

// First-boot machine (before anything is saved): the build env still picks
// it, so each board starts on the machine it's mounted on.
#if defined(ROWER_PROFILE_KUIPERS_AQUAROWER)
constexpr MachineType kDefaultMachine = MachineType::KuipersAquaRower;
#else
constexpr MachineType kDefaultMachine = MachineType::DunlopAir;
#endif
constexpr SensorType kDefaultSensor = SensorType::Reed;
constexpr PulseMode kDefaultPulseMode = PulseMode::Revolution;

} // namespace cfg
