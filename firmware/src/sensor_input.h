#pragma once

#include <cstdint>

// Abstracts the stroke/rev sensor edge source, which differs by board — see
// the "Sensor input source" comment in config.h for the 7"-vs-5" rationale.
namespace sensor_input {

void begin();

// Call every loop(). A no-op on the 7"/devkit board (ISR-driven); on the 5"
// board this is what actually reads the CH422G expander over I2C.
void poll(uint32_t nowMs);

// True once per detected pulse, then clears the flag. outPulseMicros is a
// pulse timestamp in the same units as micros() — synthesized from millis()
// on the polled (5" board) path, since that path can't resolve true
// microsecond edges and callers (PulseMode::Stroke) don't need them to.
bool consumePulse(uint32_t& outPulseMicros);

} // namespace sensor_input
