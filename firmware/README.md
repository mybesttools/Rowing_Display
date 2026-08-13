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

## Note on reuse and licensing

Because source licenses differ across candidate repos, this scaffold is written from scratch.
Use it as a neutral base and selectively add code only from repositories with licenses you accept.
