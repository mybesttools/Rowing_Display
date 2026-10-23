#include "settings_screen.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "scene_background.h"
#include "sd_backgrounds.h"
#include "ui_theme.h"

namespace settings_screen {

namespace {

using namespace ui_theme;
using settings::Settings;

// A numeric setting edited with -/+ buttons. Floats step by a percentage of
// their value (inertia ranges 0.1 to 1.9 between machines), ints by a fixed
// amount. Splits are whole seconds shown as m:ss.
struct NumField {
    const char* name;
    const char* hint;
    float (*get)(const Settings&);
    void (*set)(Settings&, float);
    bool isFloat;
    float step; // ints: absolute; floats: fraction of the value
    float min;
    float max;
    bool isSplit = false;
};


const NumField kMachineFields[] = {
    {"Pulses per wheel turn", "magnets passing the sensor per turn",
     [](const Settings& s) { return float(s.machineParams.pulsesPerRev); },
     [](Settings& s, float v) { s.machineParams.pulsesPerRev = uint8_t(v); }, false, 1, 1, 12},
    {"Flywheel inertia", "kg*m2 - scales watts and distance",
     [](const Settings& s) { return s.machineParams.flywheelInertia; },
     [](Settings& s, float v) { s.machineParams.flywheelInertia = v; }, true, 0.02f, 0.001f, 20.0f},
    {"Start drag factor", "used until the first stroke is fitted",
     [](const Settings& s) { return s.machineParams.initialDragFactor; },
     [](Settings& s, float v) { s.machineParams.initialDragFactor = v; }, true, 0.02f, 0.0001f, 5.0f},
    {"Boat constant C", "2.80 = Concept2 pace/watts relation",
     [](const Settings& s) { return s.machineParams.boatConstant; },
     [](Settings& s, float v) { s.machineParams.boatConstant = v; }, true, 0.02f, 1.0f, 10.0f},
    {"Min drive time", "ms - shorter drives are ignored",
     [](const Settings& s) { return float(s.machineParams.minDriveMs); },
     [](Settings& s, float v) { s.machineParams.minDriveMs = uint16_t(v); }, false, 25, 50, 1500},
    {"Min recovery time", "ms - shorter recoveries are ignored",
     [](const Settings& s) { return float(s.machineParams.minRecoveryMs); },
     [](Settings& s, float v) { s.machineParams.minRecoveryMs = uint16_t(v); }, false, 25, 100, 3000},
    {"Min pulse gap", "ms - faster pulses are treated as noise",
     [](const Settings& s) { return float(s.machineParams.minPulseGapMs); },
     [](Settings& s, float v) { s.machineParams.minPulseGapMs = uint16_t(v); }, false, 5, 5, 500},
};

const NumField kSensorFields[] = {
    {"Debounce", "ms the contact must be quiet before a pulse counts",
     [](const Settings& s) { return float(s.sensorParams.debounceMs); },
     [](Settings& s, float v) { s.sensorParams.debounceMs = uint16_t(v); }, false, 1, 0, 100},
};

const NumField kWorkoutFields[] = {
    {"Target split", "/500m the pace boat rows; hold to change faster",
     [](const Settings& s) { return float(s.paceBoatSplitSec); },
     [](Settings& s, float v) { s.paceBoatSplitSec = uint16_t(v); }, false, 5, 60, 595, true},
};

constexpr int kMachineFieldCount = sizeof(kMachineFields) / sizeof(kMachineFields[0]);
constexpr int kSensorFieldCount = sizeof(kSensorFields) / sizeof(kSensorFields[0]);
constexpr int kWorkoutFieldStart = kMachineFieldCount + kSensorFieldCount;
constexpr int kFieldCount = kWorkoutFieldStart + sizeof(kWorkoutFields) / sizeof(kWorkoutFields[0]);

const NumField& field(int i) {
    if (i < kMachineFieldCount) {
        return kMachineFields[i];
    }
    return i < kWorkoutFieldStart ? kSensorFields[i - kMachineFieldCount] : kWorkoutFields[i - kWorkoutFieldStart];
}

enum DropdownId : intptr_t { kDdMachine, kDdSensor, kDdMode, kDdInput, kDdBackground };

Settings g_edit;
DoneCallback g_onDone = nullptr;
lv_obj_t* g_valueLabels[kFieldCount] = {};
lv_obj_t* g_defaultLabels[kFieldCount] = {};
lv_obj_t* g_ddMachine = nullptr;
lv_obj_t* g_ddSensor = nullptr;
lv_obj_t* g_ddMode = nullptr;
lv_obj_t* g_ddInput = nullptr;
lv_obj_t* g_ddBackground = nullptr;
lv_obj_t* g_swPaceBoat = nullptr;

void formatValue(char* out, size_t cap, const NumField& f, float v) {
    if (f.isSplit) {
        const int sec = static_cast<int>(lroundf(v));
        snprintf(out, cap, "%d:%02d", sec / 60, sec % 60);
    } else if (f.isFloat) {
        snprintf(out, cap, "%.3g", v);
    } else {
        snprintf(out, cap, "%d", static_cast<int>(lroundf(v)));
    }
}

// Background dropdown: the built-in one, then the SD card's in
// sd_backgrounds order. A saved file that's no longer on the card shows as
// the built-in.
uint32_t backgroundIndex(const char* file) {
    for (int i = 0; i < sd_backgrounds::count(); i++) {
        if (strcmp(sd_backgrounds::fileName(i), file) == 0) {
            return static_cast<uint32_t>(i + 1);
        }
    }
    return 0;
}

void buildBackgroundOptions(lv_obj_t* dd) {
    lv_dropdown_set_options(dd, scene_background::kDefault.name);
    for (int i = 0; i < sd_backgrounds::count(); i++) {
        lv_dropdown_add_option(dd, sd_backgrounds::displayName(i), LV_DROPDOWN_POS_LAST);
    }
}

Settings presetFor(const Settings& s) {
    return settings::defaults(s.machine, s.sensor);
}

void refresh() {
    const Settings preset = presetFor(g_edit);
    char buf[48];
    char val[16];
    for (int i = 0; i < kFieldCount; i++) {
        const NumField& f = field(i);
        const float v = f.get(g_edit);
        const float d = f.get(preset);
        formatValue(buf, sizeof(buf), f, v);
        lv_label_set_text(g_valueLabels[i], buf);
        // Compare as displayed, so float noise doesn't flag a value as edited.
        formatValue(val, sizeof(val), f, d);
        const bool edited = strcmp(buf, val) != 0;
        lv_obj_set_style_text_color(g_valueLabels[i], edited ? kColorAccent : kColorValue, 0);
        snprintf(buf, sizeof(buf), "%s  |  default %s", f.hint, val);
        lv_label_set_text(g_defaultLabels[i], buf);
    }
    lv_dropdown_set_selected(g_ddMachine, static_cast<uint32_t>(g_edit.machine));
    lv_dropdown_set_selected(g_ddSensor, static_cast<uint32_t>(g_edit.sensor));
    lv_dropdown_set_selected(g_ddMode, static_cast<uint32_t>(g_edit.pulseMode));
    lv_dropdown_set_selected(g_ddBackground, backgroundIndex(g_edit.background));
    if (g_edit.paceBoatOn) {
        lv_obj_add_state(g_swPaceBoat, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(g_swPaceBoat, LV_STATE_CHECKED);
    }
#if defined(ROWING_BOARD_LCD_5)
    lv_dropdown_set_selected(g_ddInput, g_edit.expanderPin == cfg::DI0_EXPANDER_PIN ? 0 : 1);
#endif
}

void onStep(lv_event_t* e) {
    const intptr_t code = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
    const NumField& f = field(static_cast<int>(code >> 1));
    const bool up = code & 1;
    float v = f.get(g_edit);
    if (f.isFloat) {
        v *= up ? (1.0f + f.step) : (1.0f - f.step);
        char buf[16];
        snprintf(buf, sizeof(buf), "%.3g", v);
        v = strtof(buf, nullptr);
    } else {
        v += up ? f.step : -f.step;
    }
    if (v < f.min) {
        v = f.min;
    }
    if (v > f.max) {
        v = f.max;
    }
    f.set(g_edit, v);
    refresh();
}

void onDropdown(lv_event_t* e) {
    lv_obj_t* dd = static_cast<lv_obj_t*>(lv_event_get_target(e));
    const uint32_t sel = lv_dropdown_get_selected(dd);
    switch (reinterpret_cast<intptr_t>(lv_event_get_user_data(e))) {
    case kDdMachine:
        g_edit.machine = static_cast<cfg::MachineType>(sel);
        g_edit.machineParams = cfg::machinePreset(g_edit.machine);
        break;
    case kDdSensor:
        g_edit.sensor = static_cast<cfg::SensorType>(sel);
        g_edit.sensorParams = cfg::sensorPreset(g_edit.sensor);
        break;
    case kDdMode:
        g_edit.pulseMode = static_cast<cfg::PulseMode>(sel);
        break;
#if defined(ROWING_BOARD_LCD_5)
    case kDdInput:
        g_edit.expanderPin = sel == 0 ? cfg::DI0_EXPANDER_PIN : cfg::DI1_EXPANDER_PIN;
        break;
#endif
    case kDdBackground:
        snprintf(g_edit.background, sizeof(g_edit.background), "%s",
                 sel == 0 ? "" : sd_backgrounds::fileName(static_cast<int>(sel) - 1));
        break;
    default:
        break;
    }
    refresh();
}

void onPaceBoatSwitch(lv_event_t* e) {
    g_edit.paceBoatOn = lv_obj_has_state(static_cast<lv_obj_t*>(lv_event_get_target(e)), LV_STATE_CHECKED);
}

void onReset(lv_event_t*) {
    // Machine/sensor defaults only; background and pace boat are kept.
    Settings reset = settings::defaults(g_edit.machine, g_edit.sensor);
    memcpy(reset.background, g_edit.background, sizeof(reset.background));
    reset.paceBoatSplitSec = g_edit.paceBoatSplitSec;
    reset.paceBoatOn = g_edit.paceBoatOn;
    g_edit = reset;
    refresh();
}

void onSave(lv_event_t*) {
    settings::submit(g_edit);
    if (g_onDone) {
        g_onDone(g_edit);
    }
}

void onBack(lv_event_t*) {
    if (g_onDone) {
        g_onDone(settings::current());
    }
}

lv_obj_t* makeButton(lv_obj_t* parent, const char* text, int32_t w, int32_t h, lv_event_cb_t cb, lv_event_code_t code,
                     void* userData) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_style_bg_color(btn, kColorTile, 0);
    lv_obj_set_style_bg_color(btn, kColorAccent, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, kColorHint, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_size(btn, w, h);
    lv_obj_add_event_cb(btn, cb, code, userData);
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, kColorValue, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_center(label);
    return btn;
}

lv_obj_t* makeRow(lv_obj_t* parent) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 76);
    lv_obj_set_style_bg_color(row, kColorTile, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_pad_hor(row, 16, 0);
    lv_obj_set_style_pad_ver(row, 6, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

// Left side of a row: the name, plus a smaller hint line under it.
lv_obj_t* makeRowText(lv_obj_t* row, const char* name, const char* hint) {
    lv_obj_t* col = lv_obj_create(row);
    lv_obj_remove_style_all(col);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* nameLabel = lv_label_create(col);
    lv_label_set_text(nameLabel, name);
    lv_obj_set_style_text_color(nameLabel, kColorCaption, 0);
    lv_obj_set_style_text_font(nameLabel, &lv_font_montserrat_24, 0);

    lv_obj_t* hintLabel = lv_label_create(col);
    lv_label_set_text(hintLabel, hint);
    lv_obj_set_style_text_color(hintLabel, kColorHint, 0);
    lv_obj_set_style_text_font(hintLabel, &lv_font_montserrat_14, 0);
    return hintLabel;
}

void makeSection(lv_obj_t* parent, const char* title) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, kColorAccent, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_top(label, 8, 0);
}

lv_obj_t* makeDropdownRow(lv_obj_t* parent, const char* name, const char* hint, const char* options, DropdownId id) {
    lv_obj_t* row = makeRow(parent);
    makeRowText(row, name, hint);
    lv_obj_t* dd = lv_dropdown_create(row);
    lv_dropdown_set_options(dd, options);
    lv_obj_set_width(dd, 340);
    lv_obj_set_style_text_font(dd, &lv_font_montserrat_24, 0);
    lv_obj_set_style_bg_color(dd, kColorBg, 0);
    lv_obj_set_style_text_color(dd, kColorValue, 0);
    lv_obj_set_style_border_color(dd, kColorHint, 0);
    lv_obj_t* list = lv_dropdown_get_list(dd);
    lv_obj_set_style_text_font(list, &lv_font_montserrat_24, 0);
    lv_obj_set_style_bg_color(list, kColorTile, 0);
    lv_obj_set_style_text_color(list, kColorValue, 0);
    lv_obj_set_style_bg_color(list, kColorAccent, LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_add_event_cb(dd, onDropdown, LV_EVENT_VALUE_CHANGED, reinterpret_cast<void*>(id));
    return dd;
}

void makeNumberRow(lv_obj_t* parent, int index) {
    const NumField& f = field(index);
    lv_obj_t* row = makeRow(parent);
    g_defaultLabels[index] = makeRowText(row, f.name, f.hint);

    void* down = reinterpret_cast<void*>(static_cast<intptr_t>(index << 1));
    void* up = reinterpret_cast<void*>(static_cast<intptr_t>((index << 1) | 1));

    lv_obj_t* minus = makeButton(row, "-", 64, 56, onStep, LV_EVENT_SHORT_CLICKED, down);
    lv_obj_add_event_cb(minus, onStep, LV_EVENT_LONG_PRESSED_REPEAT, down);

    lv_obj_t* value = lv_label_create(row);
    lv_obj_set_width(value, 130);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_32, 0);
    g_valueLabels[index] = value;

    lv_obj_t* plus = makeButton(row, "+", 64, 56, onStep, LV_EVENT_SHORT_CLICKED, up);
    lv_obj_add_event_cb(plus, onStep, LV_EVENT_LONG_PRESSED_REPEAT, up);
}

} // namespace

lv_obj_t* build(DoneCallback onDone) {
    g_onDone = onDone;

    lv_obj_t* screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen, kColorBg, 0);
    lv_obj_set_style_pad_all(screen, 12, 0);
    lv_obj_set_style_pad_row(screen, 10, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* header = lv_obj_create(screen);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), 60);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 12, 0);
    makeButton(header, LV_SYMBOL_LEFT " BACK", 150, 56, onBack, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "SETTINGS");
    lv_obj_set_style_text_color(title, kColorAccent, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_flex_grow(title, 1);
    makeButton(header, "DEFAULTS", 170, 56, onReset, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* save = makeButton(header, LV_SYMBOL_OK " SAVE", 150, 56, onSave, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_style_border_color(save, kColorAccent, 0);

    lv_obj_t* list = lv_obj_create(screen);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 8, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    makeSection(list, "MACHINE");
    g_ddMachine = makeDropdownRow(list, "Machine", "loads that machine's defaults",
                                  "Kuipers AquaRower\nDunlop Air Rower", kDdMachine);
    for (int i = 0; i < kMachineFieldCount; i++) {
        makeNumberRow(list, i);
    }

    makeSection(list, "SENSOR");
    g_ddSensor = makeDropdownRow(list, "Sensor type", "loads that sensor's debounce", "Reed switch\nHall sensor",
                                 kDdSensor);
    g_ddMode = makeDropdownRow(list, "Each pulse is", "wheel turn: detect strokes from wheel speed",
                               "a wheel turn\na whole stroke", kDdMode);
#if defined(ROWING_BOARD_LCD_5)
    g_ddInput = makeDropdownRow(list, "Input terminal", "sensor wired between this and GND", "DI0\nDI1", kDdInput);
#endif
    for (int i = kMachineFieldCount; i < kWorkoutFieldStart; i++) {
        makeNumberRow(list, i);
    }

    makeSection(list, "WORKOUT");
    lv_obj_t* paceRow = makeRow(list);
    makeRowText(paceRow, "Pace boat", "a ghost boat rows the target split beside you");
    g_swPaceBoat = lv_switch_create(paceRow);
    lv_obj_set_size(g_swPaceBoat, 96, 48);
    lv_obj_set_style_bg_color(g_swPaceBoat, kColorBg, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_swPaceBoat, kColorAccent, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(g_swPaceBoat, onPaceBoatSwitch, LV_EVENT_VALUE_CHANGED, nullptr);
    for (int i = kWorkoutFieldStart; i < kFieldCount; i++) {
        makeNumberRow(list, i);
    }

    makeSection(list, "DISPLAY");
    g_ddBackground = makeDropdownRow(list, "Background", "more from the SD card's backgrounds folder; restarts",
                                     "", kDdBackground);
    buildBackgroundOptions(g_ddBackground);

    return screen;
}

void load() {
    g_edit = settings::current();
    refresh();
}

} // namespace settings_screen
