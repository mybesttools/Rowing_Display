#include <Arduino.h>
#include <cstdio>
#include <cstring>

#include "ble_ftms.h"
#include "config.h"
#include "metrics_model.h"
#include "panel_runtime.h"
#include "pulse_log.h"
#include "sensor_input.h"
#include "settings.h"
#include "waveshare_7_board.h"

namespace {

MetricsModel g_model;
BleFtmsService g_ble;

uint32_t g_lastLoopMs = 0;
uint32_t g_lastNotifyMs = 0;
uint32_t g_lastLogMs = 0;
uint32_t g_lastPulseMs = 0;
// Background the scene frames were rendered with at boot.
settings::Settings g_bootSettings;

void printMetrics(const RowerMetrics& m) {
    printf(
        "rpm=%.1f spm=%.1f watts=%.0f dist=%.1f split=%.0f strokes=%lu t=%lu k=%.5f\n", m.rpm, m.strokeRateSpm,
        m.powerWatts, m.distanceMeters, m.splitSecPer500m, static_cast<unsigned long>(m.strokeCount),
        static_cast<unsigned long>(m.elapsedSec), m.dragFactor
    );
}

void logSecond(uint32_t nowMs, const RowerMetrics& m) {
    const sensor_input::PollStats s = sensor_input::takePollStats();
    pulse_log::printf(
        "S,%lu,%lu,%lu,%lu,%lu\n", static_cast<unsigned long>(nowMs), static_cast<unsigned long>(s.reads),
        static_cast<unsigned long>(s.lockFails), static_cast<unsigned long>(s.i2cFails),
        static_cast<unsigned long>(s.maxGapMs)
    );
    pulse_log::printf(
        "M,%lu,%.1f,%.1f,%.0f,%.1f,%lu,%d,%.4f\n", static_cast<unsigned long>(nowMs), m.rpm, m.strokeRateSpm,
        m.powerWatts, m.distanceMeters, static_cast<unsigned long>(m.strokeCount), static_cast<int>(m.phase),
        m.dragFactor
    );
}

void applySettings(const settings::Settings& s) {
    ModelParams params;
    params.mode = s.pulseMode;
    params.machine = s.machineParams;
    g_model.setParams(params);
    sensor_input::configure(s.expanderPin, s.sensorParams.debounceMs);

    const cfg::MachineParams& m = s.machineParams;
    pulse_log::printf(
        "C,%lu,machine=%d,sensor=%d,mode=%d,pin=%d,ppr=%d,inertia=%.3f,drag0=%.4f,C=%.2f,drive=%d,recovery=%d,gap=%d,"
        "debounce=%d\n",
        static_cast<unsigned long>(millis()), static_cast<int>(s.machine), static_cast<int>(s.sensor),
        static_cast<int>(s.pulseMode), s.expanderPin, m.pulsesPerRev, m.flywheelInertia, m.initialDragFactor,
        m.boatConstant, m.minDriveMs, m.minRecoveryMs, m.minPulseGapMs, s.sensorParams.debounceMs
    );
    printf("settings: %s, %s\n", settings::machineName(s.machine), settings::sensorName(s.sensor));
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

    pulse_log::begin();
    pulse_log::printf("B,%lu\n", static_cast<unsigned long>(millis()));

    settings::begin();
    g_bootSettings = settings::current();
    sensor_input::begin();
    applySettings(settings::current());

    g_ble.begin("RowingDisplay-ESP32");
    panel_runtime::begin();

    printf("Rowing display starter scaffold ready\n");
}

void loop() {
    const uint32_t nowMs = millis();
    sensor_input::poll(nowMs);

    settings::Settings submitted;
    if (settings::takeSubmitted(submitted)) {
        panel_runtime::resyncDisplay(); // the NVS write stalled PSRAM
        applySettings(submitted);
        if (strcmp(submitted.background, g_bootSettings.background) != 0) {
            // Backgrounds are read from the SD card and rendered into the
            // scene frames at boot only (see panel_runtime.cpp).
            printf("settings: background changed, restarting\n");
            delay(500);
            esp_restart();
        }
    }

    if (panel_runtime::consumeSessionStartRequested()) {
        g_model.reset();
        pulse_log::printf("R,%lu\n", static_cast<unsigned long>(nowMs));
        printf("session: reset for new session\n");
    }

    uint32_t pulseMicros = 0;
    while (sensor_input::consumePulse(pulseMicros)) {
        const uint32_t pulseMs = pulseMicros / 1000;
        g_lastPulseMs = pulseMs;
        g_model.onPulseMicros(pulseMicros, pulseMs);
        pulse_log::printf("P,%lu\n", static_cast<unsigned long>(pulseMs));
    }

    if (nowMs - g_lastLoopMs >= cfg::LOOP_INTERVAL_MS) {
        g_lastLoopMs = nowMs;
        // millis(), not nowMs: pulses consumed above carry their own (core-0)
        // timestamps and can be newer than nowMs, which would underflow the
        // model's idle check and drop it to Idle mid-stroke.
        g_model.tick(millis());
        printMetrics(g_model.metrics());
        panel_runtime::update(g_model.metrics());
    }

    // millis(), not nowMs: a pulse consumed above can be newer than nowMs.
    const uint32_t sinceLastPulseMs = g_lastPulseMs == 0 ? UINT32_MAX : millis() - g_lastPulseMs;
    if (nowMs - g_lastLogMs >= 1000) {
        g_lastLogMs = nowMs;
        if (sinceLastPulseMs < 10000) {
            logSecond(nowMs, g_model.metrics());
        } else {
            sensor_input::takePollStats();
        }
    }
    if (pulse_log::poll(nowMs, sinceLastPulseMs >= 5000)) {
        panel_runtime::resyncDisplay();
    }

    if (nowMs - g_lastNotifyMs >= cfg::FTMS_NOTIFY_INTERVAL_MS) {
        g_lastNotifyMs = nowMs;
        g_ble.notify(g_model.metrics());
    }
}
