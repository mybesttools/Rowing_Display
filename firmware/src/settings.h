#pragma once

#include <cstdint>

#include "config.h"

// User-editable machine/sensor settings, starting from the factory presets
// in machine_presets.h and persisted in NVS.
namespace settings {

struct Settings {
    cfg::MachineType machine;
    cfg::SensorType sensor;
    cfg::PulseMode pulseMode;
    uint8_t expanderPin; // 5" board DI terminal (CH422G pin); unused elsewhere
    cfg::MachineParams machineParams;
    cfg::SensorParams sensorParams;
    // Scene background file on the SD card (sd_backgrounds.h); "" = built-in.
    char background[32];
    // Pace boat target split, seconds per 500 m (kept while it's off).
    uint16_t paceBoatSplitSec;
    bool paceBoatOn;
};
static_assert(sizeof(Settings) == 76, "NVS blob layout; bump kVersion and migrate when this changes");

constexpr uint16_t kDefaultPaceBoatSplitSec = 150;

// Factory defaults for a machine + sensor combination.
Settings defaults(cfg::MachineType machine, cfg::SensorType sensor);

// Loads saved settings (or first-boot defaults). Call once in setup().
void begin();
Settings current();

// From the UI task: hand new settings to the main loop, which saves and
// applies them (flash writes and model updates belong on that side).
void submit(const Settings& s);
// Main loop: true once per submit(); persists and returns the new settings.
bool takeSubmitted(Settings& out);

const char* machineName(cfg::MachineType t);
const char* sensorName(cfg::SensorType t);

} // namespace settings
