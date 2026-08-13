#include "waveshare_7_board.h"

#include <cstdio>

namespace waveshare7 {

void logBoardProfile() {
    printf("Board profile: Waveshare ESP32-S3 Touch LCD 7\n");
    printf("LCD: %s %dx%d\n", LCD_CONTROLLER, LCD_WIDTH, LCD_HEIGHT);
    printf("Touch: %s on I2C SDA=%d SCL=%d INT=%d\n", TOUCH_CONTROLLER, TOUCH_I2C_SDA, TOUCH_I2C_SCL, TOUCH_INT);
    printf("Expander: %s (backlight/reset path)\n", EXPANDER);
    printf("RGB panel, touch, and expander are initialized by panel_runtime::begin() via Board.\n");
}

} // namespace waveshare7
