#include "settings.h"

#include <Preferences.h>

#include <cstring>

#include "freertos/FreeRTOS.h"

namespace settings {

namespace {

constexpr char kNamespace[] = "rowing";
constexpr char kKey[] = "settings";
// Bump when Settings' layout changes; a mismatched blob falls back to defaults.
constexpr uint8_t kVersion = 2;

struct Stored {
    uint8_t version;
    Settings s;
};

// Version 1: Settings before `background` was added. Still loaded, so an
// update keeps the user's calibration.
struct StoredV1 {
    uint8_t version;
    cfg::MachineType machine;
    cfg::SensorType sensor;
    cfg::PulseMode pulseMode;
    uint8_t expanderPin;
    cfg::MachineParams machineParams;
    cfg::SensorParams sensorParams;
};

Settings g_current;
Settings g_submitted;
bool g_hasSubmitted = false;
portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;

uint8_t defaultExpanderPin() {
#if defined(ROWING_BOARD_LCD_5)
    return cfg::DEFAULT_EXPANDER_PIN;
#else
    return 0;
#endif
}

void persist(const Settings& s) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        printf("settings: NVS open failed, not saved\n");
        return;
    }
    const Stored stored{kVersion, s};
    prefs.putBytes(kKey, &stored, sizeof(stored));
    prefs.end();
}

} // namespace

Settings defaults(cfg::MachineType machine, cfg::SensorType sensor) {
    return Settings{
        machine,
        sensor,
        cfg::kDefaultPulseMode,
        defaultExpanderPin(),
        cfg::machinePreset(machine),
        cfg::sensorPreset(sensor),
        "",
    };
}

void begin() {
    g_current = defaults(cfg::kDefaultMachine, cfg::kDefaultSensor);

    Preferences prefs;
    if (!prefs.begin(kNamespace, true)) {
        return; // nothing saved yet
    }
    const size_t len = prefs.getBytesLength(kKey);
    Stored stored{};
    StoredV1 v1{};
    if (len == sizeof(stored) && prefs.getBytes(kKey, &stored, sizeof(stored)) == len && stored.version == kVersion) {
        g_current = stored.s;
        g_current.background[sizeof(g_current.background) - 1] = '\0';
    } else if (len == sizeof(v1) && prefs.getBytes(kKey, &v1, sizeof(v1)) == len && v1.version == 1) {
        g_current = Settings{v1.machine,       v1.sensor,       v1.pulseMode, v1.expanderPin,
                             v1.machineParams, v1.sensorParams, ""};
    }
    prefs.end();
}

Settings current() {
    portENTER_CRITICAL(&g_lock);
    const Settings s = g_current;
    portEXIT_CRITICAL(&g_lock);
    return s;
}

void submit(const Settings& s) {
    portENTER_CRITICAL(&g_lock);
    g_submitted = s;
    g_hasSubmitted = true;
    portEXIT_CRITICAL(&g_lock);
}

bool takeSubmitted(Settings& out) {
    portENTER_CRITICAL(&g_lock);
    const bool has = g_hasSubmitted;
    if (has) {
        out = g_submitted;
        g_current = g_submitted;
        g_hasSubmitted = false;
    }
    portEXIT_CRITICAL(&g_lock);
    if (has) {
        persist(out);
    }
    return has;
}

const char* machineName(cfg::MachineType t) {
    return t == cfg::MachineType::DunlopAir ? "Dunlop Air Rower 750" : "Kuipers AquaRower (water)";
}

const char* sensorName(cfg::SensorType t) {
    return t == cfg::SensorType::Hall ? "Hall sensor" : "Reed switch";
}

} // namespace settings
