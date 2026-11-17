#include "firmware_update.h"

#include <Arduino.h>

#include <cstdio>
#include <cstring>

#include "firmware_version.h"
#include "version_util.h"

// The PlatformIO environment, defined by scripts/firmware_version.py.
#ifndef FIRMWARE_VARIANT
#define FIRMWARE_VARIANT "unknown"
#endif
// Where the release workflow publishes; override with -DFIRMWARE_UPDATE_URL
// for a fork. Ends in a slash: manifest paths are relative to it.
#ifndef FIRMWARE_UPDATE_URL
#define FIRMWARE_UPDATE_URL "https://mybesttools.github.io/Rowing_Display/firmware/"
#endif

#ifdef SIMULATION_MODE

namespace firmware_update {

const char* version() {
    return kFirmwareVersion;
}

const char* variant() {
    return FIRMWARE_VARIANT;
}

Result checkAndInstall(StatusFn) {
    return Result::Skipped;
}

void markBootOk() {}

} // namespace firmware_update

#else

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "panel_runtime.h"

// The Arduino core marks a new firmware good as soon as it starts unless
// this says to wait; markBootOk() does it once the screens are up instead.
extern "C" bool verifyRollbackLater() {
    return true;
}

namespace firmware_update {

namespace {

constexpr int kTimeoutMs = 15000;
// Flash writes stall the RGB panel's frame DMA; re-align it this often while
// the download is being written (see panel_runtime::resyncDisplay()).
constexpr uint32_t kResyncEveryMs = 500;

void report(StatusFn status, const char* text) {
    if (status != nullptr) {
        status(text);
    } else {
        printf("update: %s\n", text);
    }
}

// GETs a small text file into `out`. False on any error or non-200 answer.
bool fetch(const char* url, char* out, size_t outSize) {
    esp_http_client_config_t config{};
    config.url = url;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = kTimeoutMs;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return false;
    }
    bool ok = false;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client);
        const int code = esp_http_client_get_status_code(client);
        if (code == 200) {
            size_t len = 0;
            int n = 0;
            while (len + 1 < outSize && (n = esp_http_client_read(client, out + len, outSize - 1 - len)) > 0) {
                len += n;
            }
            out[len] = '\0';
            ok = n >= 0 && len > 0;
        } else {
            printf("update: %s answered %d\n", url, code);
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

bool install(const char* url, const char* newVersion, StatusFn status) {
    esp_http_client_config_t http{};
    http.url = url;
    http.crt_bundle_attach = esp_crt_bundle_attach;
    http.timeout_ms = kTimeoutMs;
    http.keep_alive_enable = true;
    esp_https_ota_config_t config{};
    config.http_config = &http;

    esp_https_ota_handle_t ota = nullptr;
    esp_err_t err = esp_https_ota_begin(&config, &ota);
    // begin() erases the target slot: a long flash operation.
    panel_runtime::resyncDisplay();
    if (err != ESP_OK) {
        printf("update: download start failed: %s\n", esp_err_to_name(err));
        return false;
    }
    const int total = esp_https_ota_get_image_size(ota);
    int shownPercent = -1;
    uint32_t lastResync = millis();
    while ((err = esp_https_ota_perform(ota)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        if (millis() - lastResync >= kResyncEveryMs) {
            lastResync = millis();
            panel_runtime::resyncDisplay();
        }
        if (total > 0) {
            const int percent = static_cast<int>(100LL * esp_https_ota_get_image_len_read(ota) / total);
            if (percent / 10 != shownPercent / 10) {
                shownPercent = percent;
                char text[48];
                snprintf(text, sizeof(text), "installing %s: %d%%", newVersion, percent);
                report(status, text);
            }
        }
    }
    if (err == ESP_OK && esp_https_ota_is_complete_data_received(ota)) {
        // Checks the image and points the next boot at it.
        err = esp_https_ota_finish(ota);
    } else {
        esp_https_ota_abort(ota);
        if (err == ESP_OK) {
            err = ESP_FAIL;
        }
    }
    panel_runtime::resyncDisplay();
    if (err != ESP_OK) {
        printf("update: install failed: %s\n", esp_err_to_name(err));
        return false;
    }
    return true;
}

} // namespace

const char* version() {
    return kFirmwareVersion;
}

const char* variant() {
    return FIRMWARE_VARIANT;
}

Result checkAndInstall(StatusFn status) {
    // A release build is the bare number; anything else ("1.2.1+f920599",
    // "1.2.1+f920599.dirty") is a local build and stays as it is.
    int ignored[3];
    if (!version_util::parse(version(), ignored) || strchr(version(), '+') != nullptr) {
        printf("update: development build %s, not checking for updates\n", version());
        return Result::Skipped;
    }
    report(status, "checking for updates...");
    static char manifest[1024];
    if (!fetch(FIRMWARE_UPDATE_URL "latest.json", manifest, sizeof(manifest))) {
        report(status, "update check failed");
        return Result::Failed;
    }
    char latest[24];
    char path[96];
    if (!version_util::jsonString(manifest, "version", latest, sizeof(latest)) ||
        !version_util::jsonString(manifest, variant(), path, sizeof(path))) {
        printf("update: no version or no build for %s in latest.json\n", variant());
        report(status, "update check failed");
        return Result::Failed;
    }
    if (!version_util::isNewer(latest, version())) {
        printf("update: running %s, latest is %s\n", version(), latest);
        return Result::UpToDate;
    }
    char url[160];
    snprintf(url, sizeof(url), "%s%s", FIRMWARE_UPDATE_URL, path);
    printf("update: %s -> %s from %s\n", version(), latest, url);
    char text[48];
    snprintf(text, sizeof(text), "installing %s...", latest);
    report(status, text);
    if (!install(url, latest, status)) {
        report(status, "update failed, keeping this version");
        return Result::Failed;
    }
    snprintf(text, sizeof(text), "installed %s, restarting", latest);
    report(status, text);
    return Result::Installed;
}

void markBootOk() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;
    }
    esp_ota_mark_app_valid_cancel_rollback();
    panel_runtime::resyncDisplay(); // an otadata flash write
    printf("update: version %s starts fine, keeping it\n", version());
}

} // namespace firmware_update

#endif
