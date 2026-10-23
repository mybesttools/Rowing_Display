#pragma once

// microSD card access, shared by sd_backgrounds and session_log. Both
// Waveshare boards wire the card to SPI GPIO11/12/13 with chip select on
// CH422G EXIO4 (vendor 03_SD_Test); mounting follows the vendor's ESP-IDF
// example (esp_vfs_fat_sdspi_mount with gpio_cs = -1, CS held low through
// the expander).
//
// On the 5" board the CH422G normally sits in input mode for the DI sensor
// terminal, which leaves CS floating high on its pull-up. mount() pauses
// the DI polling and switches the expander to output mode for as long as
// the card is mounted; unmount() switches back. Keep mounts short and
// never mount while rowing: pulses are missed meanwhile.
namespace sd_card {

constexpr char kMountPoint[] = "/sd";

// `expander` is panel_runtime::ioExpanderBase(). True if the card is
// mounted at kMountPoint; call unmount() either way.
bool mount(void* expander);
void unmount();

} // namespace sd_card
