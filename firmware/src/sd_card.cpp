#include "sd_card.h"

#include <cstdio>

#if !defined(SIMULATION_MODE)
#include "driver/sdspi_host.h"
#include "esp_display_panel.hpp"
#include "esp_lv_adapter_arduino.h"
#include "esp_vfs_fat.h"
#include "sensor_input.h"
#endif

namespace sd_card {

#if defined(SIMULATION_MODE)

bool mount(void*) {
    return false;
}

void unmount() {}

#else

namespace {

constexpr int kSdMosi = 11;
constexpr int kSdSck = 12;
constexpr int kSdMiso = 13;
constexpr uint8_t kSdCsExpanderPin = 4; // CH422G EXIO4

esp_expander::Base* g_expander = nullptr;
sdmmc_card_t* g_card = nullptr;
bool g_busUp = false;
bool g_expanderTaken = false;
sdmmc_host_t g_host = SDSPI_HOST_DEFAULT();

bool setChipSelect(bool selected) {
    if (!esp_lv_adapter_i2c_lock(100)) {
        return false;
    }
    const bool ok = g_expander->digitalWrite(kSdCsExpanderPin, selected ? LOW : HIGH);
    esp_lv_adapter_i2c_unlock();
    return ok;
}

} // namespace

bool mount(void* expander) {
    g_expander = static_cast<esp_expander::Base*>(expander);
    if (g_expander == nullptr) {
        return false;
    }
    g_expanderTaken = sensor_input::takeExpanderForOutput();
    if (!g_expanderTaken || !setChipSelect(true)) {
        return false;
    }

    spi_bus_config_t bus{};
    bus.mosi_io_num = kSdMosi;
    bus.miso_io_num = kSdMiso;
    bus.sclk_io_num = kSdSck;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 4000;
    if (spi_bus_initialize(static_cast<spi_host_device_t>(g_host.slot), &bus, SDSPI_DEFAULT_DMA) != ESP_OK) {
        printf("sd: spi bus init failed\n");
        return false;
    }
    g_busUp = true;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = GPIO_NUM_NC;
    slot.host_id = static_cast<spi_host_device_t>(g_host.slot);
    esp_vfs_fat_sdmmc_mount_config_t config{};
    config.format_if_mount_failed = false;
    config.max_files = 2;
    config.allocation_unit_size = 16 * 1024;
    if (esp_vfs_fat_sdspi_mount(kMountPoint, &g_host, &slot, &config, &g_card) != ESP_OK) {
        printf("sd: no card (or mount failed)\n");
        g_card = nullptr;
        return false;
    }
    return true;
}

void unmount() {
    if (g_card != nullptr) {
        esp_vfs_fat_sdcard_unmount(kMountPoint, g_card);
        g_card = nullptr;
    }
    if (g_busUp) {
        spi_bus_free(static_cast<spi_host_device_t>(g_host.slot));
        g_busUp = false;
    }
    if (g_expander != nullptr) {
        setChipSelect(false);
    }
    if (g_expanderTaken) {
        sensor_input::releaseExpander();
        g_expanderTaken = false;
    }
}

#endif

} // namespace sd_card
