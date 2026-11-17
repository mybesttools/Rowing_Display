# Firmware

On-device console for both Waveshare boards. Build and flash from the repo
root; the commands are in the [root README](../README.md). `platformio.ini`
lives there too (`src_dir = firmware/src`). Wiring, presets, and the physics
model are in [Hardware.md](../Hardware.md).

## Source map

`src/main.cpp` is the loop: sensor pulses, the metrics model, the panel,
BLE notifications, session save, USB serial commands, and the clock sync.

Metrics and input:

- `src/metrics_model.*` — drag-factor fit, stroke phase, split, power, distance, averages, calories, pause
- `src/sensor_input.*` — reed/Hall edges. GPIO interrupt on the 7" board; CH422G poll on the 5" (DI1 by default)
- `src/pulse_log.*` — on-device pulse log, pulled with `scripts/fetch_pulse_log.py`
- `include/config.h`, `include/machine_presets.h` — pins and the per-machine presets. Kuipers is calibrated against its stock monitor; Dunlop inertia and boat constant are still placeholders

Display:

- `src/panel_runtime.*` — LVGL home and session screens, precomputed scene frames, the 800×480 present blit
- `src/rower_scene.*` — stern-view rower, drawn into those frames at boot
- `src/pace_boat.*` — ghost boat for the target split, drawn over each frame
- `src/scene_background.*` — the built-in photo
- `src/sd_backgrounds.*`, `src/sd_card.*` — SD card mount, extra backgrounds, shared with session files
- `src/settings.*`, `src/settings_screen.*`, `src/ui_theme.h` — Settings pages and NVS
- `src/wifi_screen.*` — network scan and the password keyboard
- `src/esp_lv_adapter_arduino.*` — LVGL on the Waveshare RGB panel, adapted from Waveshare's LVGL demo (Apache-2.0; see the file header)

Session and radios:

- `src/session_log.*` — one sample per second, written as TCX when the session ends
- `src/rtc_clock.*` — PCF85063 on the 5" board
- `src/net_time.*`, `include/timezones.h` — Wi-Fi NTP, then the radio turns off
- `src/ble_ftms.*`, `src/ftms_encoder.*` — FTMS Indoor Rower service, data, and control point

Board bring-up:

- `src/waveshare_7_board.*` — 7" profile constants and bus bootstrap

`src/stern_drive_gif.h` and `src/stern_recovery_gif.h` are leftovers from the
GIF scene. The draw path is `rower_scene`, and `platformio.ini` excludes the
old GIF sources.

## Waveshare 7-inch board

- Board profile: `ESP32-S3-Touch-LCD-7` (`esp32-s3-devkitc-1`)
- Panel: ST7262, RGB 800×480
- Touch: GT911 on I2C (SDA=8, SCL=9, INT=4)
- Sensor: GPIO6 ("Sensor AD")
- CH422G IO expander for backlight and control lines

The 5" board (`esp32-s3-lcd5`) is the one in daily use on the Kuipers. Same
panel size and the same UI. Its sensor is a CH422G digital input, and it
has the RTC. Pinout and the reed wiring are in Hardware.md.

Manufacturer references for the 7" board:

- [Manufacturer git](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7)
- [Product page](https://www.waveshare.com/esp32-s3-touch-lcd-7.htm)
- [Docs](https://docs.waveshare.net/ESP32-S3-Touch-LCD-7)

The 5" equivalents are [waveshareteam/ESP32-S3-Touch-LCD-5](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-5).

## Tests

`pio test -e native-test` runs the metrics-model tests on the host. They
construct `MetricsModel` directly and do not need a board.

## License

This project's own code (everything under `firmware/src`, `firmware/include`,
and `firmware/test`, plus the root `platformio.ini`) is licensed under the
**GNU General Public License v3.0** — see the `LICENSE` file at the repo
root.

Vendored third-party libraries under `firmware/lib/` (`ESP32_Display_Panel`,
`ESP32_IO_Expander`) keep their own upstream licenses (Apache-2.0), included
alongside each library's source. Nothing was copied from GPLv3 or
unlicensed reference projects studied during development (openrowingmonitor,
ESPRowingMonitor) — see Hardware.md's "Metrics/physics model" section for
what was independently implemented from their documented physics, and
"Reusable GitHub repositories" for the license status of each reference.
