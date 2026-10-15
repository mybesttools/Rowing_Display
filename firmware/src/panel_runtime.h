#pragma once

#include "metrics_model.h"

namespace panel_runtime {

bool begin();
void update(const RowerMetrics& metrics);

// True exactly once, the loop iteration after the user pressed a "Start
// Session" button on the home screen - the caller should reset the metrics
// model at that point so the new session starts from zero.
bool consumeSessionStartRequested();

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
