#pragma once

#include <cstdint>

// Append-only text log in the unused "spiffs" flash partition, so a rowing
// session can be pulled off later with scripts/fetch_pulse_log.py (esptool
// read_flash). Survives resets and power-off. One log per boot that saw
// rowing; the oldest are erased so at most 7 are kept (first in, first out).
namespace pulse_log {

void begin();
void printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
// Writes buffered lines to flash, but only while `quiet` (not rowing): a flash
// write stalls PSRAM and makes the RGB panel drift. While rowing, lines that
// no longer fit are dropped (counted in a D line). Returns true when it
// wrote, so the caller can re-sync the panel.
bool poll(uint32_t nowMs, bool quiet);

} // namespace pulse_log
