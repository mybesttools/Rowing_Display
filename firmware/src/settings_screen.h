#pragma once

#include "lvgl.h"
#include "settings.h"

// Touch screen for editing settings::Settings. All functions run in the
// LVGL task (call with the LVGL lock held or from an LVGL event).
namespace settings_screen {

// Called when the user leaves the screen, with the settings now in effect
// (the saved ones, or the unchanged current ones after BACK).
using DoneCallback = void (*)(const settings::Settings& inEffect);

lv_obj_t* build(DoneCallback onDone);

// Loads settings::current() into the widgets; call before showing.
void load();

} // namespace settings_screen
