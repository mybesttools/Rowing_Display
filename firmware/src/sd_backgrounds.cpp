#include "sd_backgrounds.h"

#include <cstdio>
#include <cstring>

#if !defined(SIMULATION_MODE)
#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>

#include "driver/sdspi_host.h"
#include "esp_display_panel.hpp"
#include "esp_lv_adapter_arduino.h"
#include "esp_vfs_fat.h"
#endif

namespace sd_backgrounds {

namespace {

struct Entry {
    char file[kFileNameLen];
    FileHeader header;
};

Entry g_entries[kMaxFiles];
int g_count = 0;

#if !defined(SIMULATION_MODE)

constexpr int kSdMosi = 11;
constexpr int kSdSck = 12;
constexpr int kSdMiso = 13;
constexpr uint8_t kSdCsExpanderPin = 4; // CH422G EXIO4
constexpr char kMountPoint[] = "/sd";
constexpr char kDir[] = "/sd/backgrounds";
constexpr size_t kPixelBytes = sizeof(uint16_t) * scene_background::kWidth * scene_background::kHeight;

bool setChipSelect(esp_expander::Base* expander, bool selected) {
    if (!esp_lv_adapter_i2c_lock(100)) {
        return false;
    }
    const bool ok = expander->digitalWrite(kSdCsExpanderPin, selected ? LOW : HIGH);
    esp_lv_adapter_i2c_unlock();
    return ok;
}

bool hasSuffix(const char* s, const char* suffix) {
    const size_t n = strlen(s);
    const size_t m = strlen(suffix);
    return n >= m && strcasecmp(s + n - m, suffix) == 0;
}

bool validHeader(const FileHeader& h, size_t fileSize) {
    return memcmp(h.magic, "RBG1", 4) == 0 && h.width == scene_background::kWidth &&
           h.height == scene_background::kHeight && fileSize == sizeof(FileHeader) + kPixelBytes;
}

void listDir() {
    DIR* dir = opendir(kDir);
    if (dir == nullptr) {
        printf("sd: no %s folder\n", kDir);
        return;
    }
    char path[sizeof(kDir) + kFileNameLen + 1];
    for (dirent* d = readdir(dir); d != nullptr && g_count < kMaxFiles; d = readdir(dir)) {
        const char* name = d->d_name;
        if (d->d_type == DT_DIR || !hasSuffix(name, ".bin") || strlen(name) >= kFileNameLen) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", kDir, name);
        struct stat st {};
        Entry& e = g_entries[g_count];
        FILE* f = fopen(path, "rb");
        const bool ok = f != nullptr && stat(path, &st) == 0 && fread(&e.header, sizeof(e.header), 1, f) == 1 &&
                        validHeader(e.header, static_cast<size_t>(st.st_size));
        if (f != nullptr) {
            fclose(f);
        }
        if (!ok) {
            printf("sd: skipping %s (not a background file)\n", name);
            continue;
        }
        strcpy(e.file, name);
        e.header.name[sizeof(e.header.name) - 1] = '\0';
        g_count++;
    }
    closedir(dir);
    std::sort(g_entries, g_entries + g_count, [](const Entry& a, const Entry& b) { return strcmp(a.file, b.file) < 0; });
    printf("sd: %d background(s) in %s\n", g_count, kDir);
}

bool load(const Entry& e, uint16_t* pixels, scene_background::Background& out) {
    char path[sizeof(kDir) + kFileNameLen + 1];
    snprintf(path, sizeof(path), "%s/%s", kDir, e.file);
    FILE* f = fopen(path, "rb");
    const bool ok = f != nullptr && fseek(f, sizeof(FileHeader), SEEK_SET) == 0 &&
                    fread(pixels, 1, kPixelBytes, f) == kPixelBytes;
    if (f != nullptr) {
        fclose(f);
    }
    if (!ok) {
        printf("sd: reading %s failed\n", path);
        return false;
    }
    out = scene_background::Background{e.header.name, pixels,           e.header.sunX,
                                       e.header.glint, e.header.sunColor, e.header.rippleColor};
    printf("sd: loaded background %s\n", e.file);
    return true;
}

#endif

} // namespace

bool begin(void* expanderBase, const char* wanted, uint16_t* pixels, scene_background::Background& out) {
#if defined(SIMULATION_MODE)
    (void)expanderBase;
    (void)wanted;
    (void)pixels;
    (void)out;
    return false;
#else
    auto* expander = static_cast<esp_expander::Base*>(expanderBase);
    if (expander == nullptr || !setChipSelect(expander, true)) {
        return false;
    }
    // Same setup as Waveshare's ESP-IDF 03_SD_Test: CS is held low through
    // the expander, so the SPI driver gets no CS pin (gpio_cs = -1).
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus{};
    bus.mosi_io_num = kSdMosi;
    bus.miso_io_num = kSdMiso;
    bus.sclk_io_num = kSdSck;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 4000;
    bool loaded = false;
    if (spi_bus_initialize(static_cast<spi_host_device_t>(host.slot), &bus, SDSPI_DEFAULT_DMA) != ESP_OK) {
        printf("sd: spi bus init failed\n");
        setChipSelect(expander, false);
        return false;
    }
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = GPIO_NUM_NC;
    slot.host_id = static_cast<spi_host_device_t>(host.slot);
    esp_vfs_fat_sdmmc_mount_config_t mount{};
    mount.format_if_mount_failed = false;
    mount.max_files = 2;
    mount.allocation_unit_size = 16 * 1024;
    sdmmc_card_t* card = nullptr;
    if (esp_vfs_fat_sdspi_mount(kMountPoint, &host, &slot, &mount, &card) == ESP_OK) {
        listDir();
        for (int i = 0; i < g_count && wanted != nullptr && pixels != nullptr; i++) {
            if (strcmp(g_entries[i].file, wanted) == 0) {
                loaded = load(g_entries[i], pixels, out);
                break;
            }
        }
        esp_vfs_fat_sdcard_unmount(kMountPoint, card);
    } else {
        printf("sd: no card (or mount failed)\n");
    }
    spi_bus_free(static_cast<spi_host_device_t>(host.slot));
    setChipSelect(expander, false);
    return loaded;
#endif
}

int count() {
    return g_count;
}

const char* fileName(int i) {
    return i >= 0 && i < g_count ? g_entries[i].file : "";
}

const char* displayName(int i) {
    return i >= 0 && i < g_count ? g_entries[i].header.name : "";
}

} // namespace sd_backgrounds
