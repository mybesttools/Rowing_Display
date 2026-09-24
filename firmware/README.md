# Rowing Display Firmware Starter

This folder contains a clean starter scaffold for your ESP32 rowing display firmware.

It is intentionally small and focused on the two reusable areas from external projects:

1. Rowing metric pipeline skeleton (pulse ISR -> rpm -> stroke/session metrics)
2. BLE FTMS indoor rower packet/notify flow

## Files

- `src/main.cpp`: Interrupt wiring, update loop, BLE notify scheduling
- `src/metrics_model.*`: Metric computation skeleton to calibrate for your machine
- `src/ftms_encoder.*`: Minimal Indoor Rower Data packet encoding
- `src/ble_ftms.*`: NimBLE FTMS service and characteristic notifications
- `src/waveshare_7_board.*`: Waveshare ESP32-S3-Touch-LCD-7 profile constants and bus bootstrap
- `include/config.h`: Pins and tuneable constants

## Waveshare 7-inch board notes

- Board profile: `ESP32-S3-Touch-LCD-7`
- Panel: ST7262, RGB 800x480
- Touch: GT911 on I2C (SDA=8, SCL=9, INT=4)
- IO expander path: CH422G (used for several control lines/backlight on this board family)

Manufacturer references (pin mapping, board init, demo code for this exact board):

- [Manufacturer git](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7) — firmware examples, hardware files, docs, demo/reference code
- [Product page](https://www.waveshare.com/esp32-s3-touch-lcd-7.htm)
- [Docs page](https://docs.waveshare.net/ESP32-S3-Touch-LCD-7)

Current scope of this scaffold:

- Hall sensor timing + rowing metrics + BLE FTMS are active
- Board I2C bootstrap for touch/expander is active
- Full RGB panel and touch UI driver bring-up is intentionally the next stage

## Build

```bash
cd firmware
pio run
```

Flash and monitor:

```bash
cd firmware
pio run -t upload
pio device monitor
```

## What to do next

1. Validate Hall polarity and edge choice on your sensor module.
2. Tune `METERS_PER_REV` using known timed pieces.
3. Replace placeholder power model with drag-based or calibrated model.
4. Add TFT UI task and display smoothing.
5. Expand FTMS fields and verify with Kinomap/EXR.

## License

This project's own code (everything under `firmware/src`, `firmware/include`,
and `firmware/test`, plus the root `platformio.ini` files) is licensed under
the **GNU General Public License v3.0** — see the `LICENSE` file at the repo
root.

Vendored third-party libraries under `firmware/lib/` (`ESP32_Display_Panel`,
`ESP32_IO_Expander`) keep their own upstream licenses (Apache-2.0), included
alongside each library's source. Nothing was copied from GPLv3 or
unlicensed reference projects studied during development (openrowingmonitor,
ESPRowingMonitor) — see `Hardware.md`'s "Metrics/physics model" section for
what was independently implemented from their documented physics/concepts,
and "Reusable GitHub repositories" for the license status of each reference
considered.
