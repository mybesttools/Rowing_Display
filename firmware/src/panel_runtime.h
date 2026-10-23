#pragma once

#include "metrics_model.h"

namespace panel_runtime {

// Starts the display with the splash screen up (see setSplashStatus()).
bool begin();

// Builds the home, session and settings screens, rendering the scene frames
// (several seconds), then shows the home screen. Call once after begin(),
// once anything that shouldn't overlap the frame rendering (the boot clock
// sync) is done.
void buildScreens();

// Splash screen: the boot screen, and shown over whatever is up while the
// Wi-Fi clock sync runs (Wi-Fi traffic disturbs the RGB panel, and a still,
// dark screen hides it best). hideSplash() returns to the previous screen.
void showSplash(const char* status);
void setSplashStatus(const char* status);
void hideSplash();
void update(const RowerMetrics& metrics);

// True exactly once, the loop iteration after the user pressed a "Start
// Session" button on the home screen - the caller should reset the metrics
// model at that point so the new session starts from zero.
bool consumeSessionStartRequested();

// True exactly once after the user left the session screen (the X button):
// the session is over.
bool consumeSessionEndRequested();

// Main loop: switch to the session screen (an app sent Start while the
// home or settings screen was up). No-op if it's already showing.
void showSession();

// Main loop: large centered text over the session scene ("PAUSED",
// "STOPPED"), or nullptr to hide it.
void setBanner(const char* text);

// The board's IO expander (CH422G on Waveshare boards) as a raw
// esp_expander::Base*, or nullptr before begin() / in SIMULATION_MODE.
// Returned as void* so this header stays usable without pulling in
// ESP32_Display_Panel, which the esp32-sim env ignores entirely; callers
// that need it (sensor_input.cpp, for the 5" board's expander-backed DI0)
// cast it back to esp_expander::Base.
void* ioExpanderBase();

// Re-syncs the RGB panel's DMA with the frame. Call after anything that
// suspends the cache (flash writes): the panel loses its place in the
// framebuffer and stays shifted otherwise (CONFIG_LCD_RGB_RESTART_IN_VSYNC
// is off in the prebuilt Arduino IDF).
void resyncDisplay();

} // namespace panel_runtime
