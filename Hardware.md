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

Battery-powered LCD. Reported metrics:

- Calorie consumption
- Time
- Distance
- Speed

No reliable /500 m split or power model expected from the stock unit. Distance/calories on this class of monitor are typically approximate.

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
- Store a **drag / meters factor** in NVS.
- Expose calibration via app or console long-press after mechanical install is stable.
- Tune against known pieces (timed intervals, perceived 2k effort, etc.).

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
