#include "settings.h"

#include <Preferences.h>

#include <cstddef>
#include <cstring>

#include "freertos/FreeRTOS.h"

namespace settings {

namespace {

constexpr char kNamespace[] = "rowing";
constexpr char kKey[] = "settings";
// Bump when Settings' layout changes; a mismatched blob falls back to defaults.
// 2: + background. 3: + paceBoatSplitSec (0 = off), in former padding.
// 4: + paceBoatOn, appended: versions 2 and 3 are this layout's first 76
// bytes and load as a prefix.
constexpr uint8_t kVersion = 4;

struct Stored {
    uint8_t version;
    Settings s;
};
constexpr size_t kStoredV2V3Size = 76;
static_assert(offsetof(Stored, s) + offsetof(Settings, paceBoatOn) == kStoredV2V3Size, "v2/v3 end where paceBoatOn starts");

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
        kDefaultPaceBoatSplitSec,
        false,
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
    } else if (len == kStoredV2V3Size && prefs.getBytes(kKey, &stored, len) == len &&
               (stored.version == 2 || stored.version == 3)) {
        const uint16_t split = stored.version == 3 ? stored.s.paceBoatSplitSec : 0;
        g_current = stored.s;
        g_current.paceBoatOn = split != 0;
        g_current.paceBoatSplitSec = split != 0 ? split : kDefaultPaceBoatSplitSec;
    } else if (len == sizeof(v1) && prefs.getBytes(kKey, &v1, sizeof(v1)) == len && v1.version == 1) {
        g_current = defaults(v1.machine, v1.sensor);
        g_current.pulseMode = v1.pulseMode;
        g_current.expanderPin = v1.expanderPin;
        g_current.machineParams = v1.machineParams;
        g_current.sensorParams = v1.sensorParams;
    }
    g_current.background[sizeof(g_current.background) - 1] = '\0';
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
