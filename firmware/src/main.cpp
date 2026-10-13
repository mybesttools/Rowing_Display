#include <Arduino.h>
#include <cstdio>

#include "ble_ftms.h"
#include "config.h"
#include "metrics_model.h"
#include "panel_runtime.h"
#include "sensor_input.h"
#include "waveshare_7_board.h"

namespace {

MetricsModel g_model;
BleFtmsService g_ble;

uint32_t g_lastLoopMs = 0;
uint32_t g_lastNotifyMs = 0;

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

    sensor_input::begin();

    g_ble.begin("RowingDisplay-ESP32");
    panel_runtime::begin();

    printf("Rowing display starter scaffold ready\n");
}

void loop() {
    const uint32_t nowMs = millis();
    sensor_input::poll(nowMs);

    if (panel_runtime::consumeSessionStartRequested()) {
        g_model.reset();
        printf("session: reset for new session\n");
    }

    uint32_t pulseMicros = 0;
    if (sensor_input::consumePulse(pulseMicros)) {
        g_model.onPulseMicros(pulseMicros, nowMs);
    }

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
