# Rowing Display

![The 5" display mounted on a Kuipers AquaRower, mid-session](hardware/in-action.jpg)

ESP32-based replacement console for home rowing machines: a color touch
display that replaces the stock LCD, reading stroke data from the
machine's own flywheel sensor and broadcasting live metrics over Bluetooth
(FTMS) to phones/apps/pace-boat tools, while still working standalone with
no phone present.

Two machines are in scope, each with its own preset (selectable and
tunable on the device under Settings, see Hardware.md):

- **Dunlop Air Rower 750** (air resistance, flywheel/fan)
- **Kuipers AquaRower** (water resistance, paddle-wheel tank)

The plan is a Hall sensor on both machines. The Kuipers console currently
reads that rower's stock reed. Whether a Hall can be mounted on the
paddle wheel is still to be checked on the machine; see Hardware.md.

Two board options, same UI/pipeline on both:

- Waveshare ESP32-S3-Touch-LCD-7 (800×480, dev board)
- Waveshare ESP32-S3-Touch-LCD-5 (800×480, compact)

Both boards have a LiPo battery connector with an onboard charger. Neither
board exposes battery voltage to the MCU, so the firmware has no battery
gauge. The 5" board's PCF85063 (CR927 cell) keeps the clock across power
cycles. The firmware reads it at boot for the home-screen clock and for
session file names, and sets it from USB (`scripts/set_clock.py`) or from
Wi-Fi NTP under Settings. The 7" board has no RTC; set the clock after
each boot (USB or Wi-Fi).

## What it does

- Reads the flywheel's reed or Hall sensor and derives rpm, stroke rate,
  drive/recovery phase, split, power, and distance from a flywheel-physics
  model (drag factor fitted per session from the recovery-phase decay).
  Machine, sensor, and stroke-detection settings are editable on the device
  and stored in NVS.
- Drives an LVGL UI: live metric tiles (split, rate, power, time, strokes,
  distance, calories, session averages) over a precomputed stern-view scene
  synced to the stroke. A pace boat can row a target split in the next lane.
  Extra scene photos live on the SD card.
- Notifies live metrics over BLE FTMS (Indoor Rower Data, two 12-byte
  packets). Start, Stop, Pause, and Reset from an app act on the session.
- Saves each session to the SD card as `/sessions/<date>_<time>.tcx`.

See [Hardware.md](Hardware.md) for sensor wiring, board pinouts, and the
physics model's derivation/attribution. See
[firmware/README.md](firmware/README.md) for the firmware source layout.

## Build

Requires [PlatformIO](https://platformio.org/). From the repo root:

```bash
pio run -e esp32-s3-devkitc-1          # 7" board, first boot on the Dunlop preset
pio run -e esp32-s3-devkitc-1-kuipers  # 7" board, first boot on the Kuipers preset
pio run -e esp32-s3-lcd5                # 5" board, first boot on the Dunlop preset
pio run -e esp32-s3-lcd5-kuipers        # 5" board, first boot on the Kuipers preset

pio run -e <env> -t upload              # flash
pio test -e native-test                 # run the metrics-model unit tests

# pull the on-device pulse log after a session
~/.platformio/penv/bin/python scripts/fetch_pulse_log.py --port /dev/cu.usbmodemXXXX

# set the clock used for session files (once on the 5" board: it has an RTC)
~/.platformio/penv/bin/python scripts/set_clock.py --port /dev/cu.usbmodemXXXX

# row a simulated session over USB (no rower needed)
~/.platformio/penv/bin/python scripts/sim_row.py --port /dev/cu.usbmodemXXXX --strokes 20
```

Each session is saved to the SD card as `/sessions/<date>_<time>.tcx` when
it ends; upload it to Strava, Intervals.icu or Garmin Connect and set the
activity type to rowing.

## Versioning

Releases follow [semantic versioning](https://semver.org/). The number is in
[`VERSION`](VERSION); [`CHANGELOG.md`](CHANGELOG.md) lists what changed.

Each build is stamped by `scripts/firmware_version.py`. A build of the commit
tagged `v<VERSION>` with no local changes reports just the number, e.g.
`1.0.0`. Any other build adds the commit and `.dirty` for uncommitted changes,
e.g. `1.0.0+f920599.dirty`. The version shows in the bottom-right corner of
the splash and home screens, in the boot log, and to Bluetooth apps as the
Device Information firmware revision.

To release: set `VERSION`, move the `Unreleased` notes in `CHANGELOG.md`
under the new number, commit, then `git tag v<VERSION>` and push the tag.

## Backgrounds

The scene behind the rower is a photo. The easiest way to add your own: put
JPEG photos of calm water (no boat in the foreground) in an `images` folder
on the SD card. At the next start the display turns each new one into a
background in the `backgrounds` folder, named after the file
(`home_lake.jpg` → "home lake"), and it shows up under Settings > Display.
The waterline is guessed from the reflection; if it's off, add `_h` and the
waterline height in percent to the name (`home_lake_h52.jpg`) and delete the
generated `.bin` so it's made again. PNG and progressive JPEG aren't
converted on the display; use the script below for those.

One background is built in; the card's `backgrounds` folder (FAT32, up to 16
files) holds the rest. To make them on a computer instead, with a preview to
check the waterline, install the image libraries once:

```bash
pip3 install numpy pillow
```

Then:

```bash
python3 scripts/make_background.py my_lake.jpg --name "Home lake"
```

This writes `sd_card/backgrounds/home_lake.bin` and a preview in
`background_previews/`. In the preview the red dashed line must sit where
the far shore meets the water; if it doesn't, rerun with `--horizon 0.52`
(0 = top of the photo, 1 = bottom). `--glint 0` turns off the sun
reflection, and `--out /Volumes/<card>/backgrounds` writes straight to the
card. `python3 scripts/make_background.py --help` lists every option.
`scripts/gen_scene_background.py` rebuilds the bundled set.

## License

This project's own code (`firmware/src`, `firmware/include`, `firmware/test`,
the `platformio.ini` files) is licensed under **GPL-3.0** — see
[LICENSE](LICENSE). Vendored libraries under `firmware/lib/` keep their own
upstream licenses; see [firmware/README.md](firmware/README.md) for details.
