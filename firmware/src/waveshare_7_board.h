#pragma once

#include <Arduino.h>

namespace waveshare7 {

// Based on Waveshare board profile snippets for ESP32-S3-Touch-LCD-7.
constexpr int LCD_WIDTH = 800;
constexpr int LCD_HEIGHT = 480;

constexpr int TOUCH_I2C_SDA = 8;
constexpr int TOUCH_I2C_SCL = 9;
constexpr int TOUCH_INT = 4;

// RGB data lines (D0..D15) from board config.
constexpr int RGB_D0 = 14;
constexpr int RGB_D1 = 38;
constexpr int RGB_D2 = 18;
constexpr int RGB_D3 = 17;
constexpr int RGB_D4 = 10;
constexpr int RGB_D5 = 39;
constexpr int RGB_D6 = 0;
constexpr int RGB_D7 = 45;
constexpr int RGB_D8 = 48;
constexpr int RGB_D9 = 47;
constexpr int RGB_D10 = 21;
constexpr int RGB_D11 = 1;
constexpr int RGB_D12 = 2;
constexpr int RGB_D13 = 42;
constexpr int RGB_D14 = 41;
constexpr int RGB_D15 = 40;

// Known board components.
constexpr const char* LCD_CONTROLLER = "ST7262";
constexpr const char* TOUCH_CONTROLLER = "GT911";
constexpr const char* EXPANDER = "CH422G";

void logBoardProfile();

} // namespace waveshare7
