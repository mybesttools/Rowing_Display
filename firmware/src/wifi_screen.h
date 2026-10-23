#pragma once

#include "lvgl.h"

// Pick a Wi-Fi network and type its password (for the clock sync). Runs in
// the LVGL task, like settings_screen.
namespace wifi_screen {

// Called with the chosen network, or with saved = false after BACK.
using DoneCallback = void (*)(bool saved, const char* ssid, const char* password);

lv_obj_t* build(DoneCallback onDone);

// Shows the network list and starts a scan; call before loading the screen.
void open(const char* currentSsid);

} // namespace wifi_screen
