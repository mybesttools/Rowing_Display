#include "sensor_input.h"

#include <Arduino.h>

#include <algorithm>

#include "config.h"
#include "pulse_log.h"

#if !defined(SIMULATION_MODE) && defined(ROWING_BOARD_LCD_5)
#include "esp_display_panel.hpp"
#include "esp_lv_adapter_arduino.h"
#include "panel_runtime.h"
#endif

namespace sensor_input {

namespace {

volatile uint32_t g_lastPulseMicros = 0;
volatile bool g_hasPulse = false;
PollStats g_stats{};
volatile uint32_t g_debounceMs = cfg::kReedSensor.debounceMs;
volatile uint8_t g_expanderPin = 0;

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

bool consumeImpl(uint32_t& outPulseMicros) {
    if (!g_hasPulse) {
        return false;
    }
    noInterrupts();
    outPulseMicros = g_lastPulseMicros;
    g_hasPulse = false;
    interrupts();
    return true;
}

#elif defined(ROWING_BOARD_LCD_5)

// ---- CH422G-polled backend (5" board) --------------------------------
// DIx is an expander pin read over I2C, not a raw GPIO, so there's no
// interrupt. A dedicated task on core 0 polls it: the Arduino loop shares
// core 1 with the higher-priority LVGL task, which renders for hundreds of
// ms at a time on the session screen and starved loop-based polling.
// Edges are timestamped in the task and queued, so a late loop() still sees
// correct pulse timing.
struct Edge {
    uint32_t ms;
    uint8_t level;
    bool pulse;
};

constexpr uint32_t kPollPeriodMs = 2;
constexpr UBaseType_t kQueueLen = 64;

esp_expander::Base* g_expander = nullptr;
QueueHandle_t g_edges = nullptr;

// CH422G only reports real pin levels on RD_IO while IO0-7 are in input mode
// (IO_OE=0); in output mode it reads back its own latch (always 1). The board
// init leaves them all-output (ESP_PANEL_BOARD_EXPANDER_POST_BEGIN_FUNCTION),
// so switch to input once, as Waveshare's 05_IO_Test does. Nothing writes the
// expander after init, so no other pin needs to stay driven.
bool switchToInput(esp_expander::Base* expander) {
    if (!esp_lv_adapter_i2c_lock(100)) {
        return false;
    }
    const bool ok = static_cast<esp_expander::CH422G*>(expander)->enableAllIO_Input();
    esp_lv_adapter_i2c_unlock();
    return ok;
}

void pollTask(void*) {
    bool lineLow = false;
    uint32_t lastEdgeMs = 0;
    uint32_t lastReadMs = 0;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(kPollPeriodMs));
        const uint32_t nowMs = millis();

        if (!esp_lv_adapter_i2c_lock(10)) {
            g_stats.lockFails++;
            continue;
        }
        const int level = g_expander->digitalRead(g_expanderPin);
        esp_lv_adapter_i2c_unlock();
        if (level < 0) {
            g_stats.i2cFails++;
            continue;
        }
        g_stats.reads++;
        if (lastReadMs != 0) {
            g_stats.maxGapMs = std::max(g_stats.maxGapMs, nowMs - lastReadMs);
        }
        lastReadMs = nowMs;

        const bool low = (level == LOW);
        if (low == lineLow) {
            continue;
        }
        lineLow = low;
        const bool quiet = nowMs - lastEdgeMs >= g_debounceMs;
        lastEdgeMs = nowMs;
        const Edge e{nowMs, static_cast<uint8_t>(level), quiet && low};
        xQueueSend(g_edges, &e, 0);
    }
}

void beginImpl() {}

void pollImpl(uint32_t /*nowMs*/) {
    if (g_expander) {
        return;
    }
    // Created by panel_runtime::begin(), which runs after sensor_input::begin().
    auto* expander = static_cast<esp_expander::Base*>(panel_runtime::ioExpanderBase());
    if (!expander || !switchToInput(expander)) {
        return;
    }
    g_expander = expander;
    g_edges = xQueueCreate(kQueueLen, sizeof(Edge));
    xTaskCreatePinnedToCore(pollTask, "di_poll", 3072, nullptr, 5, nullptr, 0);
}

bool consumeImpl(uint32_t& outPulseMicros) {
    if (!g_edges) {
        return false;
    }
    Edge e;
    while (xQueueReceive(g_edges, &e, 0) == pdTRUE) {
        pulse_log::printf("E,%lu,%d\n", static_cast<unsigned long>(e.ms), e.level);
        if (e.pulse) {
            outPulseMicros = e.ms * 1000;
            return true;
        }
    }
    return false;
}

#else

// ---- Hardware-ISR backend (7"/devkit board) ---------------------------
volatile uint32_t g_lastEdgeMicros = 0;

// Fires on every edge. A pulse (line pulled low) only counts when the line
// was quiet for the debounce window beforehand, so contact bounce on press *and*
// on release (edges milliseconds apart) is ignored however long the press.
void IRAM_ATTR onSensorEdge() {
    const uint32_t now = micros();
    const bool quiet = now - g_lastEdgeMicros >= g_debounceMs * 1000u;
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

bool consumeImpl(uint32_t& outPulseMicros) {
    if (!g_hasPulse) {
        return false;
    }
    noInterrupts();
    outPulseMicros = g_lastPulseMicros;
    g_hasPulse = false;
    interrupts();
    return true;
}

#endif

} // namespace

void begin() {
    beginImpl();
}

void configure(uint8_t expanderPin, uint16_t debounceMs) {
    g_expanderPin = expanderPin;
    g_debounceMs = debounceMs;
}

void poll(uint32_t nowMs) {
    pollImpl(nowMs);
}

bool consumePulse(uint32_t& outPulseMicros) {
    return consumeImpl(outPulseMicros);
}

PollStats takePollStats() {
    const PollStats s = g_stats;
    g_stats = PollStats{};
    return s;
}

} // namespace sensor_input
