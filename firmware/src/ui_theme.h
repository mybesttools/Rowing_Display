#pragma once

#include "lvgl.h"

namespace ui_theme {

// Dark background, white text, one accent reserved for the number that
// matters most (split) and for edited settings.
constexpr lv_color_t kColorBg = LV_COLOR_MAKE(0x0B, 0x0F, 0x14);
constexpr lv_color_t kColorTile = LV_COLOR_MAKE(0x15, 0x1B, 0x23);
constexpr lv_color_t kColorCaption = LV_COLOR_MAKE(0xF2, 0xF4, 0xF7);
constexpr lv_color_t kColorValue = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF);
constexpr lv_color_t kColorAccent = LV_COLOR_MAKE(0x3D, 0xE8, 0xB8);
constexpr lv_color_t kColorHint = LV_COLOR_MAKE(0x8A, 0x94, 0xA6);

} // namespace ui_theme
