#include <Arduino.h>

#include "esp_heap_caps.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ble_ftms.h"
#include "config.h"
#include "firmware_version.h"
#include "metrics_model.h"
#include "net_time.h"
#include "panel_runtime.h"
#include "pulse_log.h"
#include "rtc_clock.h"
#include "sensor_input.h"
#include "session_log.h"
#include "settings.h"
#include "timezones.h"
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
// A session runs from START SESSION (or an app's Start) until the X button
// or an app's Stop. While it runs an app may pause and resume it.
bool g_sessionActive = false;

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
    rtc_clock::setTimezone(timezones::zone(s.timezone).posix);

    const cfg::MachineParams& m = s.machineParams;
    pulse_log::printf(
        "C,%lu,machine=%d,sensor=%d,mode=%d,pin=%d,ppr=%d,inertia=%.3f,drag0=%.4f,C=%.2f,drive=%d,recovery=%d,gap=%d,"
        "debounce=%d\n",
        static_cast<unsigned long>(millis()), static_cast<int>(s.machine), static_cast<int>(s.sensor),
        static_cast<int>(s.pulseMode), s.expanderPin, m.pulsesPerRev, m.flywheelInertia, m.initialDragFactor,
        m.boatConstant, m.minDriveMs, m.minRecoveryMs, m.minPulseGapMs, s.sensorParams.debounceMs
    );
    printf("settings: %s, %s, background \"%s\", pace boat %s %u s\n", settings::machineName(s.machine),
           settings::sensorName(s.sensor), s.background, s.paceBoatOn ? "on" : "off", s.paceBoatSplitSec);
}

// Runs a Wi-Fi clock sync to the end with the splash screen up, showing its
// progress. Blocking (a few seconds, up to ~40 s when the network is
// unreachable): Wi-Fi and the RGB panel's PSRAM traffic disturb each other,
// so nothing else should be drawing meanwhile. Never called mid-session.
void syncClock(const settings::Settings& s, bool returnToScreen) {
    if (s.wifiSsid[0] == '\0' || net_time::busy()) {
        return;
    }
    panel_runtime::showSplash("Setting the clock...");
    delay(500); // let the screen switch finish drawing before the radio starts
    net_time::startSync(s.wifiSsid, s.wifiPassword, timezones::zone(s.timezone).posix);
    char shown[48] = "";
    while (net_time::busy()) {
        if (strcmp(shown, net_time::statusText()) != 0) {
            snprintf(shown, sizeof(shown), "%s", net_time::statusText());
            char line[64];
            snprintf(line, sizeof(line), "Clock: %s", shown);
            panel_runtime::setSplashStatus(line);
        }
        delay(50);
    }
    char line[64];
    snprintf(line, sizeof(line), "Clock: %s", net_time::statusText());
    panel_runtime::setSplashStatus(line);
    delay(1200); // long enough to read the result
    if (returnToScreen) {
        panel_runtime::hideSplash();
    }
}

// Weekly is plenty for an RTC (drifts seconds a week), and keeps the radio,
// which disturbs the RGB panel, off nearly all the time.
constexpr time_t kClockSyncIntervalSec = 7 * 24 * 3600;
constexpr uint32_t kClockSyncCheckMs = 60UL * 60UL * 1000UL;

bool clockSyncDue() {
    return !rtc_clock::valid() || time(nullptr) - rtc_clock::lastSet() >= kClockSyncIntervalSec;
}

void startSession(uint32_t nowMs) {
    g_model.reset();
    session_log::start();
    g_sessionActive = true;
    panel_runtime::setBanner(nullptr);
    pulse_log::printf("R,%lu\n", static_cast<unsigned long>(nowMs));
    printf("session: started\n");
    g_ble.notifyStatus(ftms::status::Event::StartedOrResumedByUser);
}

void endSession(uint32_t nowMs) {
    if (!g_sessionActive) {
        return;
    }
    g_sessionActive = false;
    g_model.pause(nowMs);
    panel_runtime::setBanner("STOPPED");
    printf("session: stopped (%lu strokes, %.0f m)\n", static_cast<unsigned long>(g_model.metrics().strokeCount),
           g_model.metrics().distanceMeters);
    g_ble.notifyStatus(ftms::status::Event::StoppedOrPausedByUser, ftms::control_point::kStop);

    char fileName[40] = "";
    switch (session_log::save(g_model.metrics(), panel_runtime::ioExpanderBase(), fileName, sizeof(fileName))) {
        case session_log::SaveResult::Saved: {
            char banner[64];
            snprintf(banner, sizeof(banner), "STOPPED\nsaved %s", fileName);
            panel_runtime::setBanner(banner);
            break;
        }
        case session_log::SaveResult::NoCard:
            panel_runtime::setBanner("STOPPED\nno SD card, not saved");
            break;
        case session_log::SaveResult::Failed:
            panel_runtime::setBanner("STOPPED\nsaving failed");
            break;
        case session_log::SaveResult::Empty:
            break;
    }
}

// USB serial commands, one per line:
//   T<unix seconds>  set the clock (scripts/set_clock.py)
//   S / E            start / end a session, like START SESSION and the X
//   P                one sensor pulse, as if from the reed (scripts/sim_row.py)
//   W                Wi-Fi scan (results in the log)
//   Y                clock sync with the saved Wi-Fi settings
//   F                forget the last clock sync, so the next boot syncs
void pollSerialCommands(uint32_t nowMs) {
    static char line[24];
    static size_t len = 0;
    while (Serial.available() > 0) {
        const int c = Serial.read();
        if (c == '\n' || c == '\r') {
            line[len] = '\0';
            if (len > 1 && line[0] == 'T') {
                rtc_clock::set(static_cast<time_t>(strtoll(line + 1, nullptr, 10)));
                panel_runtime::resyncDisplay(); // the NVS write stalled PSRAM
            } else if (len == 1 && line[0] == 'S') {
                panel_runtime::showSession();
                startSession(nowMs);
            } else if (len == 1 && line[0] == 'E') {
                endSession(nowMs);
            } else if (len == 1 && line[0] == 'W') {
                net_time::startScan();
            } else if (len == 1 && line[0] == 'Y') {
                net_time::requestSync();
            } else if (len == 1 && line[0] == 'F') {
                rtc_clock::forgetLastSet();
                panel_runtime::resyncDisplay(); // the NVS write stalled PSRAM
                printf("clock: last sync forgotten, the next boot syncs\n");
            } else if (len == 1 && line[0] == 'P') {
                const uint32_t ms = millis();
                g_lastPulseMs = ms;
                g_model.onPulseMicros(micros(), ms);
            }
            len = 0;
        } else if (len + 1 < sizeof(line)) {
            line[len++] = static_cast<char>(c);
        }
    }
}

// Carries out a Fitness Machine Control Point command from an app.
void handleAppCommand(BleFtmsService::Command command, uint32_t nowMs) {
    switch (command) {
        case BleFtmsService::Command::Start:
            if (!g_sessionActive) {
                panel_runtime::showSession();
                startSession(nowMs);
            } else if (g_model.paused()) {
                g_model.resume(nowMs);
                panel_runtime::setBanner(nullptr);
                printf("session: resumed by app\n");
                g_ble.notifyStatus(ftms::status::Event::StartedOrResumedByUser);
            }
            break;
        case BleFtmsService::Command::Pause:
            if (g_sessionActive && !g_model.paused()) {
                g_model.pause(nowMs);
                panel_runtime::setBanner("PAUSED");
                printf("session: paused by app\n");
                g_ble.notifyStatus(ftms::status::Event::StoppedOrPausedByUser, ftms::control_point::kPause);
            }
            break;
        case BleFtmsService::Command::Stop:
            endSession(nowMs);
            break;
        case BleFtmsService::Command::Reset:
            g_model.reset();
            if (g_sessionActive) {
                panel_runtime::setBanner(nullptr);
            }
            printf("session: reset by app\n");
            g_ble.notifyStatus(ftms::status::Event::Reset);
            break;
        case BleFtmsService::Command::None:
            break;
    }
}

} // namespace

void setup() {
    // Arduino's HWCDC Serial doesn't actually flush on this board/toolchain
    // combo (it silently fights the IDF secondary console, which already owns
    // the same USB-Serial-JTAG peripheral). printf()/ESP_LOG go through that
    // secondary console directly and are what actually reach the USB port, so
    // that's what all logging below uses instead of Serial.print*.
    Serial.begin(cfg::SERIAL_BAUD);
    printf("Rowing display firmware %s\n", kFirmwareVersion);

    waveshare7::logBoardProfile();

    pulse_log::begin();
    pulse_log::printf("B,%lu\n", static_cast<unsigned long>(millis()));

    settings::begin();
    g_bootSettings = settings::current();
    sensor_input::begin();
    applySettings(settings::current());

    g_ble.begin("RowingDisplay-ESP32");
    panel_runtime::begin(); // splash screen
    rtc_clock::begin();
    printf("heap: internal free %u (largest %u), psram free %u\n",
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    // Wi-Fi only for this, and off again once the clock is set (net_time.h);
    // done before the scene frames are rendered, so the two never overlap.
    if (clockSyncDue()) {
        syncClock(settings::current(), false);
    } else {
        printf("clock: last set %ld h ago, next sync in %ld h\n",
               static_cast<long>((time(nullptr) - rtc_clock::lastSet()) / 3600),
               static_cast<long>((rtc_clock::lastSet() + kClockSyncIntervalSec - time(nullptr)) / 3600));
    }

    panel_runtime::setSplashStatus("Preparing the scene...");
    panel_runtime::buildScreens();

    printf("ready\n");
}

void loop() {
    const uint32_t nowMs = millis();
    sensor_input::poll(nowMs);

    settings::Settings submitted;
    if (settings::takeSubmitted(submitted)) {
        panel_runtime::resyncDisplay(); // the NVS write stalled PSRAM
        static settings::Settings applied = g_bootSettings;
        const bool wifiChanged = strcmp(submitted.wifiSsid, applied.wifiSsid) != 0 ||
                                 strcmp(submitted.wifiPassword, applied.wifiPassword) != 0;
        applied = submitted;
        applySettings(submitted);
        if (wifiChanged) {
            syncClock(submitted, true);
        }
        if (strcmp(submitted.background, g_bootSettings.background) != 0) {
            // Backgrounds are read from the SD card and rendered into the
            // scene frames at boot only (see panel_runtime.cpp).
            printf("settings: background changed, restarting\n");
            delay(500);
            esp_restart();
        }
    }

    if (panel_runtime::consumeSessionStartRequested()) {
        startSession(nowMs);
    }
    if (panel_runtime::consumeSessionEndRequested()) {
        endSession(nowMs);
    }
    for (BleFtmsService::Command c = g_ble.takeCommand(); c != BleFtmsService::Command::None; c = g_ble.takeCommand()) {
        handleAppCommand(c, nowMs);
    }
    pollSerialCommands(nowMs);

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
        if (g_sessionActive && !g_model.paused()) {
            session_log::sample(g_model.metrics());
        }
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
    // Weekly clock sync for a display that stays on, never mid-session.
    static uint32_t lastSyncCheckMs = nowMs;
    if (nowMs - lastSyncCheckMs >= kClockSyncCheckMs) {
        lastSyncCheckMs = nowMs;
        if (!g_sessionActive && !net_time::busy() && clockSyncDue()) {
            syncClock(settings::current(), true);
        }
    }
    if (net_time::takeSyncRequest() && !g_sessionActive) {
        syncClock(settings::current(), true);
    }

    if (pulse_log::poll(nowMs, sinceLastPulseMs >= 5000)) {
        panel_runtime::resyncDisplay();
    }

    if (nowMs - g_lastNotifyMs >= cfg::FTMS_NOTIFY_INTERVAL_MS) {
        g_lastNotifyMs = nowMs;
        g_ble.notify(g_model.metrics());
    }
}
