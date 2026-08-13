#pragma once

#include "esp_lib_utils.h"

// Compatibility shim for the vendored ESP32_Display_Panel and ESP32_IO_Expander
// stack. The installed esp-lib-utils package exposes the core utilities through
// esp_lib_utils.h rather than a separate esp_utils_helpers.h header.
