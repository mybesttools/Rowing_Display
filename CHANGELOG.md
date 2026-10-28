# Changelog

Firmware releases. The version is in [`VERSION`](VERSION); see
[Versioning](README.md#versioning) in the README for how a build gets its
version string.

## Unreleased

## 1.2.0 — 2026-10-28

- Browser installer on GitHub Pages
  (https://mybesttools.github.io/Rowing_Display/): pick the display and
  machine, flash over USB from Chrome or Edge.
- Each release is built from its tag and attaches one merged image per
  board and machine, flashable at offset 0.

## 1.1.0 — 2026-10-27

- Pulse log keeps the newest 7 logs (one per boot that saw rowing) and
  erases the oldest first.

## 1.0.0 — 2026-10-25

First numbered release: the firmware as it runs on the 5" and 7" boards.

- Stroke detection from flywheel speed (reed or Hall sensor), Kuipers
  AquaRower calibrated against its original monitor; Dunlop Air Rower preset.
- Session screen with split, stroke rate, watts, time, strokes, meters,
  session averages and calories over a rendered stern-view rower scene.
- Settings on the device (machine, sensor, stroke detection, workout,
  display, clock), saved in flash.
- Scene backgrounds from the SD card, and photos on the card converted at
  boot.
- Bluetooth FTMS rower for phone apps, with control point
  start/stop/pause/reset and Device Information (firmware revision).
- Sessions saved to the SD card as TCX files.
- Pace boat.
- Clock from the RTC (5" board), synced weekly over Wi-Fi, with time zones.
- Version shown on the splash and home screens and in the boot log.
