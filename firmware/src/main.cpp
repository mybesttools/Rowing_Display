#include <Arduino.h>
#include <cstdio>

#include "ble_ftms.h"
#include "config.h"
#include "metrics_model.h"
#include "panel_runtime.h"
#include "waveshare_7_board.h"

namespace {

volatile uint32_t g_lastPulseMicros = 0;
volatile bool g_hasPulse = false;

bool g_lastHallLevel = true;

MetricsModel g_model;
BleFtmsService g_ble;

uint32_t g_lastLoopMs = 0;
uint32_t g_lastNotifyMs = 0;

void IRAM_ATTR onHallEdge() {
    g_lastPulseMicros = micros();
    g_hasPulse = true;
}

void printMetrics(const RowerMetrics& m) {
    printf(
        "rpm=%.1f spm=%.1f watts=%.0f dist=%.1f split=%.0f strokes=%lu t=%lu k=%.5f\n", m.rpm, m.strokeRateSpm,
        m.powerWatts, m.distanceMeters, m.splitSecPer500m, static_cast<unsigned long>(m.strokeCount),
        static_cast<unsigned long>(m.elapsedSec), m.dragFactor
    );
}

} // namespace

void setup() {
    // Arduino's HWCDC Serial doesn't actually flush on this board/toolchain
    // combo (it silently fights the IDF secondary console, which already owns
    // the same USB-Serial-JTAG peripheral). printf()/ESP_LOG go through that
    // secondary console directly and are what actually reach the USB port, so
    // that's what all logging below uses instead of Serial.print*.
    Serial.begin(cfg::SERIAL_BAUD);

    waveshare7::logBoardProfile();

    pinMode(cfg::HALL_PIN, INPUT_PULLUP);
    g_lastHallLevel = digitalRead(cfg::HALL_PIN);
    attachInterrupt(digitalPinToInterrupt(cfg::HALL_PIN), onHallEdge, FALLING);

    g_ble.begin("RowingDisplay-ESP32");
    panel_runtime::begin();

    printf("Rowing display starter scaffold ready\n");
}

void loop() {
#ifdef SIMULATION_MODE
    static uint32_t nextPulseUs = 0;
    const uint32_t nowUs = micros();
    if (nextPulseUs == 0 || static_cast<int32_t>(nowUs - nextPulseUs) >= 0) {
        g_lastPulseMicros = nowUs;
        g_hasPulse = true;
        // Roughly 24 RPM when MAGNETS_PER_REV=2: pulse every 1.25s.
        nextPulseUs = nowUs + 1250000u;
    }
#endif

    if (panel_runtime::consumeSessionStartRequested()) {
        g_model.reset();
        printf("session: reset for new session\n");
    }

    // Poll fallback: Waveshare routes GPIO6 to the Sensor AD header (ADC in
    // their demo). Digital edges usually work, but a light poll catches
    // missed interrupts when the line is probed by hand.
    const bool hallLevel = digitalRead(cfg::HALL_PIN);
    if (!hallLevel && g_lastHallLevel) {
        g_lastPulseMicros = micros();
        g_hasPulse = true;
    }
    g_lastHallLevel = hallLevel;

    if (g_hasPulse) {
        noInterrupts();
        const uint32_t pulseMicros = g_lastPulseMicros;
        g_hasPulse = false;
        interrupts();
        g_model.onPulseMicros(pulseMicros, millis());
    }

    const uint32_t nowMs = millis();
    if (nowMs - g_lastLoopMs >= cfg::LOOP_INTERVAL_MS) {
        g_lastLoopMs = nowMs;
        g_model.tick(nowMs);
        printMetrics(g_model.metrics());
        panel_runtime::update(g_model.metrics());
    }

    if (nowMs - g_lastNotifyMs >= cfg::FTMS_NOTIFY_INTERVAL_MS) {
        g_lastNotifyMs = nowMs;
        g_ble.notify(g_model.metrics());
    }
}
