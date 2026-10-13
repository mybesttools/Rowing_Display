#include "sensor_input.h"

#include <Arduino.h>

#include "config.h"

#if !defined(SIMULATION_MODE) && defined(ROWING_BOARD_LCD_5)
#include "esp_display_panel.hpp"
#include "panel_runtime.h"
#endif

namespace sensor_input {

namespace {

volatile uint32_t g_lastPulseMicros = 0;
volatile bool g_hasPulse = false;

#if defined(SIMULATION_MODE)

// ---- Simulated backend -------------------------------------------------
// No real sensor attached; synthesize a pulse on a fixed cadence so the rest
// of the pipeline (metrics model, BLE, panel) has something to chew on.
uint32_t g_nextPulseUs = 0;

void beginImpl() {}

void pollImpl(uint32_t /*nowMs*/) {
    const uint32_t nowUs = micros();
    if (g_nextPulseUs == 0 || static_cast<int32_t>(nowUs - g_nextPulseUs) >= 0) {
        g_lastPulseMicros = nowUs;
        g_hasPulse = true;
        // Roughly 24 RPM when MAGNETS_PER_REV=2: pulse every 1.25s.
        g_nextPulseUs = nowUs + 1250000u;
    }
}

#elif defined(ROWING_BOARD_LCD_5)

// ---- CH422G-polled backend (5" board) --------------------------------
// DI0 has no hardware interrupt (it's an I2C-read expander pin, not a raw
// GPIO — see config.h), so edges are found by polling instead of an ISR.
esp_expander::Base* g_expander = nullptr;
bool g_lineLow = false;
uint32_t g_lastEdgeMs = 0;
uint32_t g_lastPollMs = 0;

void tryAcquireExpander() {
    if (!g_expander) {
        g_expander = static_cast<esp_expander::Base*>(panel_runtime::ioExpanderBase());
    }
}

void beginImpl() {
    // panel_runtime::begin() (which creates the board/expander) may not have
    // run yet at this call site; pollImpl() retries the acquisition lazily.
    tryAcquireExpander();
}

void pollImpl(uint32_t nowMs) {
    if (nowMs - g_lastPollMs < cfg::HALL_POLL_INTERVAL_MS) {
        return;
    }
    g_lastPollMs = nowMs;

    tryAcquireExpander();
    if (!g_expander) {
        return;
    }

    const int level = g_expander->digitalRead(cfg::HALL_EXPANDER_PIN);
    if (level < 0) {
        return; // I2C read failed; try again next poll
    }

    const bool low = (level == LOW);
    if (low == g_lineLow) {
        return; // no edge
    }
    g_lineLow = low;

    const bool quiet = nowMs - g_lastEdgeMs >= cfg::HALL_POLL_QUIET_MS;
    g_lastEdgeMs = nowMs;
    if (quiet && low) {
        g_lastPulseMicros = nowMs * 1000;
        g_hasPulse = true;
    }
}

#else

// ---- Hardware-ISR backend (7"/devkit board) ---------------------------
volatile uint32_t g_lastEdgeMicros = 0;

// Fires on every edge. A pulse (line pulled low) only counts when the line
// was quiet for PULSE_QUIET_US beforehand, so contact bounce on press *and*
// on release (edges milliseconds apart) is ignored however long the press.
void IRAM_ATTR onSensorEdge() {
    const uint32_t now = micros();
    const bool quiet = now - g_lastEdgeMicros >= cfg::PULSE_QUIET_US;
    g_lastEdgeMicros = now;
    if (quiet && digitalRead(cfg::HALL_PIN) == LOW) {
        g_lastPulseMicros = now;
        g_hasPulse = true;
    }
}

void beginImpl() {
    pinMode(cfg::HALL_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(cfg::HALL_PIN), onSensorEdge, CHANGE);
}

void pollImpl(uint32_t /*nowMs*/) {}

#endif

} // namespace

void begin() {
    beginImpl();
}

void poll(uint32_t nowMs) {
    pollImpl(nowMs);
}

bool consumePulse(uint32_t& outPulseMicros) {
    if (!g_hasPulse) {
        return false;
    }
    noInterrupts();
    outPulseMicros = g_lastPulseMicros;
    g_hasPulse = false;
    interrupts();
    return true;
}

} // namespace sensor_input
