#include "net_time.h"

#include <Arduino.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "esp_heap_caps.h"
#include "esp_sntp.h"
#include "panel_runtime.h"
#include "rtc_clock.h"

namespace net_time {

namespace {

constexpr uint32_t kConnectTimeoutMs = 15000;
constexpr uint32_t kSyncTimeoutMs = 10000;

struct Network {
    char name[33];
    int rssi;
};

volatile State g_state = State::Idle;
char g_status[48] = "";
Network g_networks[kMaxNetworks];
int g_networkCount = 0;

struct Job {
    bool scan;
    char ssid[33];
    char password[65];
    char tz[48];
};
Job g_job;

void setStatus(State s, const char* text) {
    snprintf(g_status, sizeof(g_status), "%s", text);
    g_state = s;
    printf("wifi: %s\n", text);
}

void radioOff() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    // Wi-Fi start-up can write PHY calibration to NVS: a flash write, which
    // shifts the RGB panel's frame (panel_runtime::resyncDisplay()).
    panel_runtime::resyncDisplay();
}

void scan() {
    WiFi.mode(WIFI_STA);
    const int n = WiFi.scanNetworks();
    g_networkCount = 0;
    for (int i = 0; i < n; i++) {
        const String name = WiFi.SSID(i);
        if (name.length() == 0 || name.length() >= sizeof(Network::name)) {
            continue;
        }
        int found = -1;
        for (int j = 0; j < g_networkCount; j++) {
            if (strcmp(g_networks[j].name, name.c_str()) == 0) {
                found = j;
            }
        }
        if (found >= 0) {
            g_networks[found].rssi = max(g_networks[found].rssi, static_cast<int>(WiFi.RSSI(i)));
        } else if (g_networkCount < kMaxNetworks) {
            snprintf(g_networks[g_networkCount].name, sizeof(Network::name), "%s", name.c_str());
            g_networks[g_networkCount].rssi = WiFi.RSSI(i);
            g_networkCount++;
        }
    }
    WiFi.scanDelete();
    // Strongest first.
    for (int i = 1; i < g_networkCount; i++) {
        for (int j = i; j > 0 && g_networks[j].rssi > g_networks[j - 1].rssi; j--) {
            const Network t = g_networks[j];
            g_networks[j] = g_networks[j - 1];
            g_networks[j - 1] = t;
        }
    }
    radioOff();
    for (int i = 0; i < g_networkCount; i++) {
        printf("wifi:   %s (%d dBm)\n", g_networks[i].name, g_networks[i].rssi);
    }
    char text[48];
    if (n < 0) {
        snprintf(text, sizeof(text), "scan failed");
    } else {
        snprintf(text, sizeof(text), "%d networks found", g_networkCount);
    }
    setStatus(State::ScanDone, text);
}

void sync() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_job.ssid, g_job.password);
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < kConnectTimeoutMs) {
        delay(100);
    }
    if (WiFi.status() != WL_CONNECTED) {
        const wl_status_t st = WiFi.status();
        radioOff();
        setStatus(State::Failed, st == WL_NO_SSID_AVAIL ? "network not found" : "can't connect (password?)");
        return;
    }

    setStatus(State::Syncing, "getting the time...");
    // configTzTime, not configTime: the latter replaces TZ with a fixed offset.
    configTzTime(g_job.tz, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    const uint32_t syncStart = millis();
    while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && millis() - syncStart < kSyncTimeoutMs) {
        delay(100);
    }
    const bool ok = sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED;
    esp_sntp_stop();
    radioOff();
    if (!ok) {
        setStatus(State::Failed, "no answer from time server");
        return;
    }
    rtc_clock::set(time(nullptr));
    const time_t now = time(nullptr);
    tm local{};
    localtime_r(&now, &local);
    char text[48];
    strftime(text, sizeof(text), "synced %H:%M", &local);
    setStatus(State::Synced, text);
}

void task(void*) {
    if (g_job.scan) {
        scan();
    } else {
        sync();
    }
    vTaskDelete(nullptr);
}

// Callers check busy() and set the state before calling this.
bool startJob() {
    WiFi.persistent(false); // credentials live in our settings, not Wi-Fi's own NVS
    // Core 0 with the sensor poll task, below its priority (5).
    if (xTaskCreatePinnedToCore(task, "net_time", 6144, nullptr, 2, nullptr, 0) == pdPASS) {
        return true;
    }
    printf("wifi: task create failed; internal heap free %u, largest block %u\n",
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    return false;
}

} // namespace

void startSync(const char* ssid, const char* password, const char* posixTz) {
    if (busy() || ssid == nullptr || ssid[0] == '\0') {
        return;
    }
    g_job.scan = false;
    snprintf(g_job.ssid, sizeof(g_job.ssid), "%s", ssid);
    snprintf(g_job.password, sizeof(g_job.password), "%s", password != nullptr ? password : "");
    snprintf(g_job.tz, sizeof(g_job.tz), "%s", posixTz);
    setStatus(State::Connecting, "connecting...");
    if (!startJob()) {
        setStatus(State::Failed, "couldn't start");
    }
}

void startScan() {
    if (busy()) {
        return;
    }
    g_job.scan = true;
    setStatus(State::Scanning, "scanning...");
    if (!startJob()) {
        setStatus(State::Failed, "couldn't start");
    }
}

State state() {
    return g_state;
}

bool busy() {
    const State s = g_state;
    return s == State::Scanning || s == State::Connecting || s == State::Syncing;
}

const char* statusText() {
    return g_status;
}

int networkCount() {
    return g_state == State::Scanning ? 0 : g_networkCount;
}

const char* networkName(int i) {
    return i >= 0 && i < g_networkCount ? g_networks[i].name : "";
}

int networkRssi(int i) {
    return i >= 0 && i < g_networkCount ? g_networks[i].rssi : -100;
}

} // namespace net_time
