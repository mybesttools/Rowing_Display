#pragma once

#include <ctime>

// Wall-clock time for session files and the home screen. The 5" board has a
// PCF85063 RTC with a coin cell (I2C 0x51, on the touch/expander bus);
// begin() copies it into the system clock. It's set over Wi-Fi (net_time.h)
// or by scripts/set_clock.py over USB (a "T<unix seconds>" line, main.cpp).
// The RTC and system clock hold UTC; the time zone only affects local time.
namespace rtc_clock {

// Call after panel_runtime::begin() (the I2C bus is up then).
void begin();

// True once the system clock holds a real date (from the RTC or set()).
bool valid();

// Sets the system clock and, if present, the RTC. UTC seconds.
void set(time_t utc);

// POSIX TZ string (timezones.h) for localtime().
void setTimezone(const char* posix);

} // namespace rtc_clock
