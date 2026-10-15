#pragma once

#include <cstdint>

// Abstracts the stroke/rev sensor edge source, which differs by board — see
// the "Sensor input source" comment in config.h for the 7"-vs-5" rationale.
namespace sensor_input {

void begin();

// Debounce window and (5" board) which CH422G pin to read. Safe to call any
// time; the polling task/ISR picks the new values up on its next edge.
void configure(uint8_t expanderPin, uint16_t debounceMs);

// Call every loop(). A no-op on the 7"/devkit board (ISR-driven); on the 5"
// board it starts the core-0 polling task once the expander exists.
void poll(uint32_t nowMs);

// True once per detected pulse; call until false (the 5" board queues them).
// outPulseMicros is the pulse timestamp in the same units as micros() — synthesized from millis()
// on the polled (5" board) path, since that path can't resolve true
// microsecond edges and callers (PulseMode::Stroke) don't need them to.
bool consumePulse(uint32_t& outPulseMicros);

// Poll-coverage counters since the last call (5" board only; zeros elsewhere).
struct PollStats {
    uint32_t reads;
    uint32_t lockFails;
    uint32_t i2cFails;
    uint32_t maxGapMs; // longest stretch between two successful reads
};
PollStats takePollStats();

} // namespace sensor_input
