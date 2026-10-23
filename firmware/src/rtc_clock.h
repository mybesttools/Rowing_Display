#pragma once

#include <ctime>

// Wall-clock time for session files. The 5" board has a PCF85063 RTC with a
// coin cell (I2C 0x51, on the touch/expander bus); begin() copies it into
// the system clock. Nothing sets it automatically: scripts/set_clock.py
// sends the computer's time over USB (a "T<unix seconds>" line, see main.cpp).
namespace rtc_clock {

// Call after panel_runtime::begin() (the I2C bus is up then).
void begin();

// True once the system clock holds a real date (from the RTC or set()).
bool valid();

// Sets the system clock and, if present, the RTC. UTC seconds.
void set(time_t utc);

} // namespace rtc_clock
