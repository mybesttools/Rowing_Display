# Dunlop Air Rower 750 — Hardware & Project Specs

Living hardware notes for replacing the stock console and rotation sensor with an ESP32-based color display system.

## Machine (stock)

| Item | Value |
|------|--------|
| Model | Dunlop Air Rower 750 |
| Resistance | Air (fan / flywheel) |
| Drive | Chain |
| Class | Home / domestic fitness (Concept2-class marketing; not a Concept2) |
| Weight | ~20 kg |
| Assembled size | 212 × 53 × 75 cm (L × W × H) |
| Foldable | Yes |
| Max user | 135 kg, 195 cm |

### Stock display computer

Battery-powered LCD (~1024×576 in a user recording of `rowersession.mp4`). Observed UI:

- **Layout:** scenic water/hills background with semi-transparent metric tiles overlaid. Central vertical lane holds a white rowing-figure icon; horizontal lines in that lane scroll to suggest forward motion.
- **Primary metric:** **SPLIT /500m** at top center (large).
- **Secondary tiles:** **SPM**, **METERS**, **WATTS** (or similar), **TIME**, stroke/calorie counters — all shown as large numerals in frosted boxes.
- **Rowing figure:** minimalist white silhouette, stern/top-down perspective. Arms and oars sweep through catch → drive → finish → recovery; motion is tied to the stroke rather than a decorative loop.
- **Idle:** figure holds a recovery-ish pose while the water/lane animation continues.
- **Modes:** single session screen (no mode cycling observed in the sample clip); values start at zero before rowing begins.

Reported metrics (marketing/spec sheet):

- Calorie consumption
- Time
- Distance
- Speed

No reliable /500 m split or power model expected from the stock unit. Distance/calories on this class of monitor are typically approximate.

Our replacement UI mirrors this layout in interactive mode (split prominent, SPM/meters/watts/time tiles, animated stern scene behind low-opacity tiles) but uses a flywheel-physics model for split/watts instead of the stock approximations.

### Stock sensing (to be removed)

Typical for this machine class (confirm on open-up):

- **2-wire magnetic reed switch** near the fan cage
- One or more magnets on the flywheel / fan
- Cable to the rear of the console

**Decision:** full replacement of sensor and monitor — do not reuse the stock reed or LCD.

---

## Project goals

1. Remove stock console and stock rotation sensor completely.
2. Install a **Hall-effect** rotation sensor + magnet(s) on the air fan.
3. Install an **ESP32** processing unit with a **color TFT** as the primary console.
4. Stream live metrics to a phone/tablet app (BLE preferred).
5. Console must work **without** a phone present.

---

## Target architecture

```
Fan cage: magnet(s) → Hall sensor → ESP32 → color TFT (primary UI)
                              ↓
                         BLE → phone / tablet app
                              ↑
                         USB-C 5 V power
```

| Layer | Role |
|-------|------|
| Hall + magnet | Revolution pulses |
| ESP32 | Timing, stroke/metrics math, display, BLE |
| Color TFT | Always-on primary readout |
| App | Mirror, history, calibration, workouts |
| USB-C 5 V | Power (not the old battery tray) |

---

## Sensor (new)

| Item | Spec / choice |
|------|----------------|
| Type | Hall effect (prefer 3.3 V logic, 3-wire: VCC, GND, OUT) |
| Examples | AH3144-class or equivalent modern Hall |
| Magnet | Small neodymium on fan blade or hub |
| Pulses per rev | **Start with 1 magnet** |
| Air gap | ~1–3 mm, mechanically fixed |
| MCU input | GPIO + interrupt; `INPUT_PULLUP` if open-collector |
| Debounce | Light software debounce if needed (Hall is cleaner than reed) |

### Why Hall (not reed)

- No contact bounce / wear
- Cleaner edges for interrupt timing
- Better long-term reliability under vibration

### Mounting notes (750 fan housing)

- Bracket sensor to frame/shroud (3D-print or metal); avoid long-term zip-tie-only mounts.
- Keep cable clear of chain and moving parts; strain-relieve at exit to console mast.
- Mount ESP32 at the **console**, not inside the dusty/hot fan cage.
- Prefer a single small magnet; check for obvious fan imbalance after install.

### Sensor wiring differs by board

The 7"/devkit board exposes a dedicated 3-pin "Sensor AD" header (3V3/GND/AD)
wired straight to a raw ESP32 pin (GPIO6). The **5" board (Waveshare
ESP32-S3-Touch-LCD-5)** has no equivalent header — instead it has a 16-way
screw terminal:

```
VIN GND VOUT GND | SDA SCL | CAN_L CAN_H | 485_B 485_A | DO0 DO1 DI_COM GND DI0 DI1
```

`DI0`/`DI1` are **opto-isolated digital inputs read through the board's
CH422G I2C IO expander**, not a raw GPIO: DI0→CH422G EXIO0, DI1→CH422G
EXIO5 (Waveshare's `05_IO_Test` sample in
[waveshareteam/ESP32-S3-Touch-LCD-5](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-5)).

Input circuit, from the board schematic (`hardware/schematics/
ESP32-S3-Touch-LCD-5-Sch.pdf`, "DI/DO" block): the board feeds `DI_COM`
itself (5V → 1N5822 → 220Ω → `DI_COM`), then 3.6kΩ → a PC814 opto LED →
the `DIx` terminal. The opto's transistor pulls EXIO0/EXIO5 to GND against
a 4.7kΩ pull-up to 3V3. So **contact closed = reads 0, open = reads 1**.

Firmware facts (`firmware/src/sensor_input.cpp`):

- CH422G's IO0-7 share one direction bit, and RD_IO only reflects the real
  pin levels in **input** mode — in output mode it reads back its own latch
  (always 1). The board init leaves IO0-7 all-output, so the firmware calls
  `enableAllIO_Input()` once after the display is up, as Waveshare's sample
  does. Nothing writes the expander after init, so display/backlight/touch
  are unaffected (verified on hardware 2026-10-15).
- Touch (GT911) and the CH422G share I2C host 0 (SCL 9 / SDA 8). LVGL reads
  touch from its own task under `esp_lv_adapter_lock()`; DI reads take the
  same lock, otherwise they collide and hit the CH422G driver's 10 ms
  timeout (`read_input_reg failed`).

**Reed switch wiring on the 5" board** (dry contact): one wire to `DI1`
(firmware currently reads DI1 — `cfg::HALL_EXPANDER_PIN = 5`), the other to
**`GND`**. Leave `DI_COM` unconnected. Wiring the switch across `DIx` and
`DI_COM` shorts out the opto LED and can never trigger (this was the
original, wrong instruction here); grounding `DI_COM` just removes the
supply. Don't use `DO0`/`DO1` (outputs).

### Other 5" board connectors (not wired for the sensor, noted for later)

- **RTC battery**: a CR927 coin-cell holder (labeled `RTC`) backs a
  PCF85063 RTC chip, keeping time across power cycles. Optional — not
  currently used by the firmware (no RTC driver wired up).
- **Main battery**: a separate 2-pin JST `Battery` connector + `Battery
  Switch` + onboard charge circuit (`CHG`/`DONE` LEDs) exists for running
  the board off a LiPo instead of USB-C. Distinct from the RTC coin cell.
  Current install runs on a 250 mAh LiPo here, charged over USB-C.
  **No battery level is readable by firmware on either board** — confirmed
  from both boards' schematics (CS8501 charger IC on both; its `CHRG`/
  `STDBY`/`DONE` status pins drive LEDs directly, no GPIO fan-out). The 7"
  board has a bare `VBAT` voltage divider (200K/100K) ending at an
  unpopulated test point (`TP1`), not wired to any pin; the 5" board
  doesn't even have that. Every raw GPIO on both boards is already claimed
  (display/touch/SD/RS485/CAN/USB, plus the RTC interrupt on the 5" board's
  `GPIO6` — the pin the 7" uses for "Sensor AD"). Showing battery level
  would need a hardware modification: either an external low-battery
  threshold circuit into the 5" board's still-unused `DI1` terminal (binary
  low-battery flag only, reuses the same CH422G read already built for the
  reed sensor), or sacrificing one of this project's unused GPIOs (RS485/
  CAN, not wired up in firmware) and bodge-wiring it to `VBAT` for an
  actual ADC voltage/percentage reading.
- **TF/SD card**: slot in the top-left corner. SPI on GPIO11 (MOSI)/
  GPIO12 (SCK)/GPIO13 (MISO), CS via CH422G EXIO4 (10K pull-up). The 7"
  board is wired the same (vendor `03_SD_Test` for both boards). Used for
  scene backgrounds, see below.

### SD card backgrounds (implemented)

`python3 scripts/gen_scene_background.py` crops/dithers the images listed in
the script: the first is built into the firmware, the rest are written to
`sd_card/backgrounds/*.bin` (56-byte header + 400×240 RGB565, 192 KB each;
format in `firmware/src/sd_backgrounds.h`). Copy the `backgrounds` folder to
the root of a FAT32 card. At boot `sd_backgrounds::begin()` lists the folder
and reads the selected file into the (not yet used) 800×480 present buffer,
so it costs no PSRAM; the scene frames are rendered from it and the card is
unmounted. The card is only read at boot because CS needs the CH422G in
output mode, and the 5" board's DI polling switches it to input right after.
Picking another background in Settings saves and restarts. No card, or the
saved file missing: built-in background. Mounting follows Waveshare's
ESP-IDF example (`esp_vfs_fat_sdspi_mount`, `gpio_cs = -1`, CS held low on
EXIO4).

---

## Monitor / MCU (new)

| Item | Spec / choice |
|------|----------------|
| MCU | **ESP32-S3** preferred (PSRAM helpful for LVGL) |
| Fallback | ESP32 / ESP32-C3 if UI is lighter |
| Display | 3.2–3.5" SPI color TFT (e.g. ILI9488 / ST7796 class) |
| UI toolkit | LVGL (recommended) |
| Mount | Replace stock console on existing stalk; custom plate/enclosure |
| Environment | Sweat-resistant enclosure; conformal coat optional |

### Power

- **USB-C 5 V** supply to the console area (wall adapter or power bank).
- Color TFT + ESP32 (BLE/Wi‑Fi) is not suitable for the stock AA/AAA tray.
- Regulate to 3.3 V for MCU/Hall as required by the module.

### Optional later

- Capacitive touch on TFT
- Physical buttons (reset / units / backlight)
- Wi‑Fi for OTA updates
- FTMS BLE profile for third-party fitness apps

### Candidate board check: Waveshare ESP32-S3 7" Capacitive Touch

Board under consideration:

- Waveshare ESP32-S3 7-inch capacitive touch display dev board
- 800x480 LCD, 5-point touch, Wi-Fi + BLE

Assessment for this rowing project:

- **Usability:** Very good for large, glanceable workout metrics from seated distance.
- **Performance headroom:** Good. ESP32-S3 + PSRAM class boards usually handle LVGL and BLE together if the UI is optimized.
- **Power:** Expect materially higher current draw than 3.2-3.5" SPI TFT builds. Use stable 5 V supply with margin.
- **Mechanical fit risk:** 7" may be large for the stock console mast and can interfere with handle movement if mounted too close.
- **Firmware complexity:** Higher than SPI TFT modules. These RGB touch boards often need vendor/BSP-specific display+touch init.
- **Environment risk:** Large glossy panels can be harder to read in bright light and need sweat splash protection.

Recommendation:

- **Buy** for this project if your priority is a premium, easy-to-read on-device console and you accept extra integration effort.
- **Skip for first prototype if** you want fastest bring-up; a 3.5" SPI TFT is easier for initial sensor/metrics validation.

Current project note:

- Power supply budget is not a constraint, so the higher panel current draw is acceptable.

Pre-buy checklist:

- Confirm the exact Waveshare SKU has:
     - ESP32-S3 variant with enough PSRAM for your UI plan.
     - Public examples for your preferred stack (Arduino or ESP-IDF + LVGL).
     - Exposed GPIO suitable for Hall sensor input.
     - Known touch controller support in examples (for calibration/settings UI later).
- Plan a 5 V supply budget for display backlight + ESP32 peak load.
- Dry-fit cardboard dimensions at the console mast before final mount design.

---

## Firmware responsibilities (ESP32)

1. **Pulse capture** — timestamp Hall edges (ISR / hardware timer).
2. **RPM** — from pulse period and magnets-per-rev.
3. **Stroke detection** — flywheel speed rises on drive, decays on recovery → stroke count & SPM.
4. **Session metrics** — elapsed time, strokes, SPM.
5. **Distance & split** — integrate calibrated factor → meters and time/500 m.
6. **Optional watts** — from spin-down / drag between strokes.
7. **Local UI** — primary always-on display.
8. **BLE** — stream smoothed metrics (~5–10 Hz), not raw pulse rate.

### Calibration

Stock and DIY air-rower meters are not Concept2-accurate by default.

- Treat distance/split as **internally consistent** first.
- Tune against known pieces (timed intervals, perceived 2k effort, etc.).

### On-device settings (implemented)

Home screen → **SETTINGS**. Picking a machine or sensor type loads its
factory defaults (`firmware/include/machine_presets.h`); each value can then
be adjusted with −/+ (hold to repeat). Values that differ from the default
show in the accent color, with the default under each row. **DEFAULTS**
restores the defaults for the selected machine + sensor; **SAVE** stores the
settings in NVS (`firmware/src/settings.cpp`) and applies them at once.
The build env (`-kuipers` or not) only decides the machine preset on first
boot. Every boot and save writes a `C,` line to the pulse log.

Machine presets:

| Setting | Kuipers AquaRower | Dunlop Air Rower 750 | What it does |
|---|---|---|---|
| Pulses per wheel turn | 1 | 1 | magnets passing the sensor per turn; scales rpm/speed/watts |
| Flywheel inertia (kg·m²) | 1.9 | 0.10 | turns the fitted recovery slope into drag factor k; scales watts (∝k) and distance (∝k^⅓) |
| Start drag factor | 0.026 | 0.035 | k used until the first recovery fit lands |
| Boat constant C | 2.80 | 3.7 | v = (k/C)^⅓·ω; 2.80 = Concept2's P = 2.80·v³ |
| Min drive time (ms) | 250 | 200 | a drive must last this long before recovery can start |
| Min recovery time (ms) | 600 | 400 | a recovery must last this long before the next stroke counts |
| Min pulse gap (ms) | 40 | 40 | faster pulses are treated as noise |

Kuipers values are calibrated (2026-10-15, against the original monitor:
354 m in 2:00; 1.33 m per wheel turn, ~73 W at 29 spm). Dunlop inertia and C
are still uncalibrated placeholders.

Sensor presets and other settings:

| Setting | Default | Options |
|---|---|---|
| Sensor type | Reed switch | Reed switch (debounce 15 ms), Hall sensor (debounce 2 ms) |
| Debounce (ms) | from sensor type | line must be quiet this long before an edge counts |
| Each pulse is | a wheel turn | a wheel turn (strokes detected from wheel speed), a whole stroke |
| Input terminal (5" board only) | DI1 | DI0, DI1 |
| Background | Dusk lake (built in) | plus any `.bin` in the SD card's `/backgrounds` folder; saving a different one restarts the display. DEFAULTS leaves it alone |

Deliberately not in the UI (algorithm internals, fixed in `config.h`):
drag-fit plausibility band, drag-fit smoothing, regression point count/R²,
rpm smoothing, idle timeout, and the per-stroke-mode tuning.

### Metrics/physics model (implemented)

`firmware/src/metrics_model.cpp` replaced an early flat-formula placeholder
(`power = rpm² × constant`) with a flywheel-physics model — drive/recovery
phase detection from pulse-period trend, a per-stroke drag factor fit during
recovery, and power/distance derived from that drag factor rather than a
made-up curve.

**Inspired by** (concepts and physics formulas only — no code copied; see the
"Reusable GitHub repositories" section below for full license notes on each):

- [openrowingmonitor](https://github.com/JaapvanEkris/openrowingmonitor) (GPLv3) — the drag-factor-as-linear-regression
  derivation and the `P = k·ω³` power model, both documented in that project's
  physics docs (itself citing Anu Dudhia's "The Physics of ErgoMeters").
- [ESPRowingMonitor](https://github.com/Abasz/ESPRowingMonitor) (no LICENSE file, treated as
  all-rights-reserved) — general shape of an embedded, pulse-timing-driven
  drive/recovery state machine.

**Governing physics**: during recovery the flywheel decelerates under drag
alone, `I·dω/dt = -k·ω²`, which integrates to `1/ω` being linear in elapsed
time with slope `k/I`. Each recovery phase collects `(t, 1/ω)` samples and
fits that line (OLS); a fit is only accepted if it clears an R² quality gate
and a plausibility range, then blended into a running drag factor via EMA.
Power is `P = k·ω³`; linear speed is `v = (k/C)^(1/3)·ω`, the same
cube-root flywheel-to-"boat" scaling Concept2-style ergometers use.

**Two constants still need real calibration** (currently placeholders in
`firmware/include/config.h`, prominently flagged in comments there):

- `FLYWHEEL_INERTIA_KGM2` — the Dunlop fan flywheel's actual moment of
  inertia (kg·m²). Scales every fitted drag factor and therefore every watts
  number. Needs a physical measurement (bifilar pendulum swing test) or
  iterative tuning once real numbers are on screen.
- `BOAT_SCALING_CONSTANT` (`C`) — empirical, per-machine, not derivable from
  first principles. Tune against known-distance test pieces, same as the
  flat `METERS_PER_REV` constant it replaced.

Validated so far: a synthetic unit test (`firmware/test/test_logic/test_main.cpp`)
confirms the OLS fit recovers a known analytically-constructed drag factor;
on real hardware (hand-waved magnet, not yet a mounted flywheel) the fitted
drag factor visibly locks onto a stable value across strokes once a few
clean recovery phases are seen, and stays put through further noisy input —
the R²/plausibility gating is doing its job.

### On-device UI (implemented)

`firmware/src/panel_runtime.cpp` now renders a real LVGL screen instead of
the earlier raw-`drawBitmap` bar-graph placeholder: large SPLIT time up top
(the one number that matters mid-stroke), a row of SPM/METERS/WATTS tiles,
and a smaller TIME/STROKES row underneath. Confirmed live on hardware: all
fields update in real time as the Hall sensor pulses.

The LVGL↔`esp_display_panel` bridge (`firmware/src/esp_lv_adapter_arduino.*`)
is adapted from Waveshare's own `10_lvgl_v9_demo` example for this exact
board (Apache-2.0 — see the file headers for the full attribution note).
Unlike the GPLv3/no-license research sources elsewhere in this project,
Apache-2.0 explicitly permits reuse with attribution, so this one **is**
adapted code, not a clean-room reimplementation — it includes the
double-framebuffer full-refresh path (`configFrameBufferNumber(2)` +
`configRGB_BounceBufferSize()` before `board->begin()`) that avoids the
screen-tear issue this board's own docs warn about with a naive
single-buffer RGB setup, which our earlier raw-`drawBitmap` code was
running without.

Next layer, not yet built: the boat/water-scene visualization discussed
alongside this metrics screen — deferred until after the metrics
foundation (this screen) was solid, per the agreed scoping.

---

## App (planned)

| Item | Direction |
|------|-----------|
| Link | BLE GATT custom service first |
| Role | Live mirror, history, targets, calibration |
| Constraint | iOS background BLE is stricter — design for foreground workout use first |
| Later | FTMS rower service for broader app compatibility |

Example live fields: `t`, `strokeRate`, `split`, `distance`, `watts`, `drag`.

---

## Minimal BOM

- ESP32-S3 DevKit (or module once pinout is frozen)
- 3.3 V Hall sensor + neodymium magnet
- 3.2–3.5" SPI TFT
- USB-C 5 V supply
- Wire, heat-shrink, strain relief
- Console enclosure / 3D-printed mount for stalk
- Optional: phone/tablet shelf if retaining a secondary screen

## Order list (ready to buy)

Status:

- Ordered: Waveshare 7" ESP32-S3 display board, Hall sensors
- Already available: remaining listed items
- **Two-unit plan confirmed** (2026-08-13): a second Waveshare ESP32-S3 7"
  display board is being ordered for the Kuipers AquaRower in Amsterdam,
  alongside the existing Dunlop Air Rower build in Germany. 4 Hall sensors
  are on hand from the original order — enough for both builds plus spares,
  so no new Hall sensor purchase is needed for the second unit.

### Core electronics (required)

- 1x Waveshare ESP32-S3 7" capacitive touch display board (800x480, Wi-Fi/BLE)
- 2x Hall-effect sensor module, 3.3 V compatible (AH3144-class or equivalent)
- 10x Neodymium magnets (small disc style, e.g. 6-8 mm diameter)
- 1x 5 V power supply, stable output with margin for 7" panel (wall adapter)
- 1x USB power cable matching the board power connector

### Wiring and protection (required)

- 1x Assorted Dupont/JST jumper wire kit (male/female mix)
- 1x Heat-shrink tubing kit
- 1x Cable sleeve or spiral wrap for cable routing along rower frame
- 1x Pack of adhesive cable tie mounts + cable ties
- 1x Small inline connector set (for easy console removal/service)

### Mechanical mounting (required)

- 1x Mount plate material (3D print filament or thin aluminum/plastic plate)
- 1x Fastener kit (M3 screws, nuts, washers, standoffs)
- 1x Threadlocker (low strength) for vibration resistance
- 1x VHB double-sided tape roll (for non-structural retention)

### Recommended spares (strongly recommended)

- 1x Extra Hall sensor (spare)
- 1x Extra USB power cable (spare)
- 1x Extra magnet pack (for placement tests and backup)

### Optional but useful

- 2-3x Physical push buttons (menu/reset/calibration)
- 1x Small piezo buzzer (stroke/beep feedback)
- 1x On/off inline USB switch
- 1x Sweat-resistant clear cover / screen protector for 7" panel

### Tools (if not already owned)

- Soldering iron + solder
- Wire stripper/cutter
- Crimp tool (if using JST connectors)
- Multimeter
- Caliper (for mount dimensions)

### Pre-check before placing order

- Confirm the exact board SKU includes public examples for your chosen firmware stack.
- Confirm available GPIO access for Hall input wiring.
- Confirm board mounting hole pattern and physical dimensions for your rower mast adapter.

---

## Decisions locked

| Decision | Choice |
|----------|--------|
| Machine | Dunlop Air Rower 750 |
| Stock sensor | **Replace** (do not reuse reed) |
| Stock monitor | **Replace** (do not reuse LCD) |
| New sensor | Hall + magnet(s), start with 1 PPR |
| MCU | ESP32 family (S3 preferred) |
| Primary UI | On-device color TFT |
| Phone required? | **No** for a usable workout |
| Power | USB-C 5 V |

## Open / to confirm on hardware

- [ ] Exact Hall part number and output type (push-pull vs open-drain)
- [ ] Final TFT size vs stalk clearance / handle path
- [ ] Magnet mount location on this fan geometry
- [ ] Console mechanical adapter dimensions
- [ ] Pin map (Hall GPIO, SPI TFT, any buttons)
- [ ] BLE UUID / packet format
- [ ] Distance model (simple meters-per-rev vs drag-factor spin-down)

---

## Build order

1. Hall + magnet + ESP32 — serial log of pulse intervals.
2. Confirm **one clean pulse per revolution** (hand spin + rowing).
3. SPM + elapsed time on serial.
4. TFT: SPM, time, stroke count.
5. Permanent brackets + cable routing; remove stock console/sensor.
6. Distance/split after first calibration.
7. BLE streaming + app.

---

## References / context

- Machine listing notes (Joysport-style product copy): chain drive, foldable, basic computer metrics.
- Similar home air rowers commonly use reed + magnet; full replacement avoids bounce and stock console limits.
- Related class docs (not identical unit): CARE “Air Rower” style manuals show console mast + optional tablet holder — useful for mount inspiration only.

---

## Reusable GitHub repositories (shortlist)

These are the strongest reusable codebases found for this exact stack: ESP32 + rowing metrics + BLE FTMS.

### 1) OpenRowingMonitor (active fork)

- Repo: https://github.com/JaapvanEkris/openrowingmonitor
- Fit for this project:
     - Mature rowing physics engine (stroke detection, split, watts, drag-related metrics)
     - Validated against Concept2 PM5 according to project docs
     - FTMS Rower + Concept2 PM emulation patterns
- Notes:
     - Runs on Raspberry Pi/Node.js, not ESP32 directly
     - Best reused as algorithm/reference source, then port selected logic to ESP32 C/C++
- License:
     - GPLv3 (copyleft). If you copy code directly, your distributed derivative must comply with GPLv3.

### 2) openrowingmonitor-ESP32S3-ZephyrOS

- Repo: https://github.com/Jannuel-Dizon/openrowingmonitor-ESP32S3-ZephyrOS
- Fit for this project:
     - ESP32-S3 + embedded C++ implementation of OpenRowingMonitor-style logic
     - Includes FTMS service and ISR/threaded architecture ideas
     - Already structured around flywheel sensor timing and rowing metrics
- Notes:
     - Uses Zephyr RTOS, not Arduino/ESP-IDF style app structure
     - Good source for module boundaries and FTMS packet flow
- License:
     - Apache-2.0 (per repo LICENSE). Most permissive option in this shortlist.

### 3) echelon-ftms-bridge

- Repo: https://github.com/welshmike/echelon-ftms-bridge
- Fit for this project:
     - Clear ESP32 FTMS rower implementation with practical notification cadence
     - Simple single-file firmware for understanding full BLE bridge flow quickly
     - Good reference for handling real device packet parsing and smoothing
- Notes:
     - Echelon-protocol specific; reuse FTMS transport patterns, not device parsing
     - README claims MIT, but no LICENSE file was found at repo root at scan time
- License:
     - Treat as unclear until LICENSE file is added/confirmed by maintainer

### 4) fdf-ftms-esp32

- Repo: https://github.com/avilleret/fdf-ftms-esp32
- Fit for this project:
     - ESP32-S3 USB-host-to-FTMS bridge with complete Indoor Rower Data packing
     - Useful FTMS encoding reference and BLE service setup
- Notes:
     - USB console bridge architecture differs from Hall-sensor flywheel capture
     - Best reused for FTMS characteristic packing and app compatibility checks
- License:
     - README says MIT, but no LICENSE file was found at repo root at scan time

### 5) qdomyos-zwift (large ecosystem reference)

- Repo: https://github.com/cagnulein/qdomyos-zwift
- Fit for this project:
     - Battle-tested interoperability logic with many apps/devices
     - Useful protocol behavior references and conversion heuristics
- Notes:
     - Large, broad codebase; not a drop-in for this firmware
     - Best used as behavior/reference source only
- License:
     - GPLv3 (copyleft)

### 6) esp32-concept2

- Repo: https://github.com/ygelfand/esp32-concept2
- Fit for this project:
     - Focused ESP32 C++ component set: FTMS encoding, USB PM host, CSAFE/Dircon pieces
     - Good low-level references for clean FTMS packet composition
- Notes:
     - Early-stage project with minimal top-level docs
     - No LICENSE file found at scan time
- License:
     - Treat as no-reuse-until-clarified for direct code copy

### 7) ESPRowingMonitor

- Repo: https://github.com/Abasz/ESPRowingMonitor
- Fit for this project:
     - ESP32-native (PlatformIO + Arduino framework + `pioarduino` platform + NimBLE-Arduino) — same toolchain as this project, unlike every other entry above
     - Ships a `genericAir` rower profile — calibration tuning for air-resistance flywheel machines, matching the Dunlop's resistance type
     - Implements JaapvanEkris's improved OpenRowingMonitor stroke-detection algorithm, adapted for embedded/interrupt-driven pulse capture
     - Clean board-profile vs. rower-profile separation (pins/hardware vs. per-machine physical calibration constants) — worth mirroring regardless of code reuse
- Notes:
     - Actively maintained, PlatformIO-based, directly comparable architecture to this repo
- License:
     - **No LICENSE file found in the repo** (checked via GitHub API, no `license` field either) — treat as all-rights-reserved. Do not copy code directly; used here for architecture/algorithm inspiration only (see "Metrics/physics model" section above for what was actually built from it).

### 8) rowing_cpu

- Repo: https://github.com/rsboni/rowing_cpu
- Fit for this project:
     - Also a water-tank paddle-wheel rower (three paddles rotating in a tank) — directly relevant to the Kuipers AquaRower profile
     - Real-world data point: used **5 magnets** on the drive wheel (not 1) with a reed switch for adequate resolution on this class of machine — initially adopted for the Kuipers profile; the logged sessions of 2026-10-15 showed the AquaRower's stock reed gives 1 pulse per turn, so the preset uses 1
- Notes:
     - The repo is a React/Firebase web dashboard only — no ESP32 firmware source is actually in the repo, just a README description of one
     - Stroke/SPM detection is a simple fixed-threshold + timer heuristic, no physics model — less sophisticated than what's already implemented here, so no algorithmic upgrade from this one
     - Last updated Jan 2023, 1 star — treat as a minor/stale reference, not an actively maintained project
- License:
     - **No LICENSE file found in the repo** — treat as all-rights-reserved. Cited here for the magnet-count data point only, no code viewed beyond a skim of `App.js`/`canvas.jsx` to confirm what it does, nothing reused.

### Recommended reuse strategy for this project

1. Start from `openrowingmonitor-ESP32S3-ZephyrOS` for architecture and FTMS flow.
2. Borrow metric model concepts from `JaapvanEkris/openrowingmonitor` and re-implement in your codebase (clean-room style if you want permissive licensing).
3. Use `echelon-ftms-bridge` and `fdf-ftms-esp32` as FTMS packet/compatibility references only.
4. Avoid direct copy from repos without explicit LICENSE files until clarified.

### Copy/fork options

- Fork for active tracking:
     - `JaapvanEkris/openrowingmonitor`
     - `Jannuel-Dizon/openrowingmonitor-ESP32S3-ZephyrOS`
     - `welshmike/echelon-ftms-bridge`
- Vendor selective files (preferred):
     - Keep your firmware lean by extracting only FTMS encoder patterns + metric formulas you actually need.

---

## Changelog

| Date | Note |
|------|------|
| 2026-08-11 | Initial specs from stock machine notes + architecture decisions (full sensor + monitor replacement, ESP32, Hall, TFT, BLE). |
| 2026-08-11 | Added reusable GitHub repository shortlist, license risk notes, and fork/copy strategy for implementation planning. |
| 2026-08-11 | Updated Waveshare 7-inch board recommendation to "buy" since power supply constraints are not a concern. |
| 2026-08-13 | Replaced the placeholder rpm²-based metrics model with a flywheel-physics model (drag factor via recovery-phase regression, `P = k·ω³`, cube-root distance scaling), clean-room inspired by openrowingmonitor and ESPRowingMonitor (see "Reusable GitHub repositories" and "Metrics/physics model" above). Added ESPRowingMonitor to the repo shortlist. |
| 2026-08-13 | Two machines confirmed in scope: the Dunlop Air Rower (flywheel, Germany) and a Kuipers AquaRower (water resistance, adjustable 12-21L tank, Amsterdam) — added a rower-profile split so firmware/calibration constants can target either. |
