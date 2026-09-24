/*
 * Project-local board selection for ESP32_Display_Panel.
 * This file is discovered by the library via __has_include.
 */

#pragma once

#define ESP_PANEL_BOARD_DEFAULT_USE_SUPPORTED       (1)

#if ESP_PANEL_BOARD_DEFAULT_USE_SUPPORTED
// Default is the 7" board; build with -DROWING_BOARD_LCD_5 for the 5"
// ESP32-S3-Touch-LCD-5 (800x480, same pinout, so the UI is unchanged).
#if defined(ROWING_BOARD_LCD_5)
#define BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_5
#else
#define BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_7
#endif

#define ESP_PANEL_BOARD_SUPPORTED_FILE_VERSION_MAJOR 1
#define ESP_PANEL_BOARD_SUPPORTED_FILE_VERSION_MINOR 0
#define ESP_PANEL_BOARD_SUPPORTED_FILE_VERSION_PATCH 0
#endif
