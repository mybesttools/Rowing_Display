# Hardware notes

Living notes for the ESP32 console that replaces the stock monitor. Two
machines are in scope: the **Dunlop Air Rower 750** (air fan) and the
**Kuipers AquaRower** (water tank). The console in daily use is the 5"
board on the Kuipers, currently reading that machine's stock reed switch.
The plan is a Hall sensor on both machines. The Kuipers mount is not
settled; see "Hall sensor on the Kuipers". Build commands are in the root
README; the source map is in `firmware/README.md`.

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

### Stock sensing

Typical for this machine class (confirm on open-up):

- **2-wire magnetic reed switch** near the fan cage
- One or more magnets on the flywheel / fan
- Cable to the rear of the console

**Decision:** replace the sensor and the monitor on this machine with a Hall sensor and one magnet. The Kuipers gets the same sensor. That rower still has its stock reed fitted; the mount is the open question.

---

## Project goals

1. Replace the stock console with an on-device color display. **Done** (Waveshare 5" and 7" boards).
2. Read the flywheel with a **Hall sensor on both machines**, one magnet per turn, strokes detected from speed-up and slow-down. **The Kuipers currently runs on its stock reed** (one pulse per turn, calibrated). The Dunlop fan can take a bracket and a magnet. The Kuipers paddle-wheel mount is still to be checked on the machine.
3. Derive split, power, distance, rate, and calories on the device. **Done.** The Kuipers preset is calibrated against its stock monitor. The Dunlop preset is not.
4. Stream live metrics to phone apps over BLE FTMS, and accept Start / Stop / Pause / Reset. **Done.** There is no companion app; history is the TCX file on the SD card.
5. A full session must work with no phone present. **Done.**

---

## Target architecture

```
Flywheel Hall (Kuipers: stock reed until the mount is checked) → ESP32-S3 → 800×480 touch panel
                              ↓
                    BLE FTMS → phone app
                              ↓
                    SD card ← session TCX, scene backgrounds
                              ↑
                    USB-C or the board's LiPo
```

| Layer | Role |
|-------|------|
| Hall, one magnet | One pulse per wheel turn. Kuipers still on its stock reed. Settings can still treat each pulse as a whole stroke |
| ESP32-S3 | Timing, stroke and metrics math, display, BLE, session file |
| 800×480 panel | Always-on primary readout |
| FTMS | Live data and session control for existing fitness apps |
| SD card | TCX when the session ends; optional scene backgrounds |
| USB-C or LiPo | Power. No battery gauge on either board |

---

## Sensor (new)

| Item | Spec / choice |
|------|----------------|
| Type | Hall effect (prefer 3.3 V logic, 3-wire: VCC, GND, OUT) |
| Examples | AH3144-class or equivalent modern Hall |
| Magnet | Small neodymium on fan blade or hub |
| Pulses per rev | **Start with 1 magnet** |
| Air gap | ~1–3 mm, mechanically fixed |
| MCU input | 7" board: GPIO6, `INPUT_PULLUP`, active low. 5" board: open-drain into `DI1`. See below |
| Debounce | Light software debounce if needed (Hall is cleaner than reed) |

### Why Hall (not reed)

- No contact bounce / wear
- Cleaner edges for interrupt timing
- Better long-term reliability under vibration

### Hall sensor on the Kuipers

The plan is the same part on both rowers. On the Dunlop the fan cage is open: a magnet on the fan, a Hall on the shroud, a 3-wire lead to the console. On the Kuipers the stock reed already sees one magnet once per paddle-wheel turn, and that is what the preset was calibrated against. Public photos of the AquaRower show the polycarbonate tank and not the sensor pocket, so three things have to be checked on the machine before buying a bracket:

1. **The cable.** The reed lead is two wires, and the console end is a 2-pin pigtail into `DI1` and `GND`. A Hall needs a supply wire as well. Either a third conductor can be pulled up the mast, or the whole lead gets replaced.
2. **The gap.** A glass reed fires across a wider gap than a small Hall switch. If the magnet is inside the tank and the reed sits outside a thick wall, a Hall in that same clip may never switch. Measure the gap, and see whether a bare sensor (a small transistor package, the size of the reed) fits the clip. A breakout board will not.
3. **The pole and the water.** A unipolar Hall switches for one pole only. If the magnet is sealed into the wheel and cannot be turned over, the part has to be omnipolar. The pocket also has to stay dry. The reed is a sealed glass capsule; a Hall in the same place needs a seal the reed never did.

The 5" input can take the Hall once those are settled. `DI1` is an opto: the reed's job is to connect `DI1` to `GND` and light the LED. An **open-drain** Hall output does that same connection (`OUT` to `DI1`, `GND` to `GND`). A push-pull output drives `DI1` high and can leave the LED half-lit, so it is the wrong kind. Power comes from the terminal's `VOUT` pin, which ships at 3.3 V (the I2C-header supply; a resistor on the board selects 5 V instead). A classic A3144 needs about 4.5 V, so it does not run from `VOUT` as the board sits. Use a 3.3 V open-drain part. The LED current is set by the 220 Ω and 3.6 kΩ resistors in series with it, and a small Hall switch can sink that.

The 7" header is `3V3` / `GND` / GPIO6 with the pin pulled up, and the firmware counts an active-low edge. The same 3.3 V open-drain part works there. The Hall preset in Settings is a 2 ms debounce; pulse mode stays "wheel turns" and pulses per turn stays 1 until a logged session says otherwise. No firmware change is required for the experiment.

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
  does. Mounting the SD card (backgrounds at boot, and the session save)
  borrows the expander back into output mode and returns it to input
  afterwards; see "Session files on the SD card".
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

- **RTC battery**: a CR927 coin-cell holder (labeled `RTC`) backs the
  PCF85063. `rtc_clock.cpp` reads it at boot. The home-screen clock and
  session file names use that time. Set it with `scripts/set_clock.py` or
  from Settings → Clock & Wi-Fi. The 7" board has no RTC in this firmware.
  See "Clock over Wi-Fi, time zone" for the battery-failed indication.
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
  would need a hardware modification. `DI0` and `DI1` are the sensor
  terminals (the reed uses one of them; Settings picks which), so there is
  no free digital input left for a low-battery flag. A voltage reading
  means sacrificing an unused GPIO (RS485/CAN, not used by this firmware)
  and wiring it to a divider on `VBAT`.
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
and reads the selected file into the 800×480 present buffer. That buffer
is only needed once a session starts, so the background costs no extra
PSRAM; the scene frames are rendered from it and the card is unmounted. The card is only read at boot because CS needs the CH422G in
output mode, and the 5" board's DI polling switches it to input right after.
Picking another background in Settings saves and restarts.

**Photos converted on the display** (`image_import.cpp`, `background_convert.cpp`):
at boot, before listing `/backgrounds`, every `.jpg`/`.jpeg` in `/images`
without a matching `/backgrounds/<slug>.bin` is converted: decoded with the
ESP32-S3 ROM TJpgDec (no flash cost; baseline JPEG only) at the coarsest
1/2^n scale that keeps ≥ 640×384, into PSRAM (frames aren't allocated yet
at that point), then the same steps as `background_tools.py`: mirror-line
waterline guess (or `_hNN` in the file name), brightest-sky sun column,
crop, box-filter resample (Lanczos in Python), sky colours, Floyd–Steinberg
to RGB565, glint 0.6. Checked on the host against the Python reference on
six photos: identical waterline/sun guesses, colours within 1/255, mean
pixel difference ~3/255. PNG is skipped: it must be decoded whole (~4 MB
RGBA for 1328×800), too much for the PSRAM left. Up to 16 photos per boot. Users make their
own with `scripts/make_background.py` (waterline detected from the
reflection's mirror symmetry, checked in a preview PNG; README "Backgrounds"). No card, or the
saved file missing: built-in background. Mounting follows Waveshare's
ESP-IDF example (`esp_vfs_fat_sdspi_mount`, `gpio_cs = -1`, CS held low on
EXIO4).

---

## Monitor / MCU (new)

| Item | Spec / choice |
|------|----------------|
| MCU | **ESP32-S3** on the Waveshare boards below (PSRAM required for the RGB framebuffers) |
| Display | Waveshare 5" and 7", both 800×480 RGB touch. The 3.2–3.5" SPI TFT option was not built |
| UI toolkit | LVGL |
| Mount | 5" board in a printed case on the Kuipers stalk |
| Environment | Sweat-resistant enclosure; conformal coat optional |

### Power

- **USB-C 5 V** supply to the console area (wall adapter or power bank).
- Color TFT + ESP32 (BLE/Wi‑Fi) is not suitable for the stock AA/AAA tray.
- Regulate to 3.3 V for MCU/Hall as required by the module.

### On the boards in use

- Capacitive touch runs Settings and session start/stop. There are no physical buttons.
- Wi-Fi is used for NTP, then turned off. There is no OTA.
- FTMS Indoor Rower is implemented. See "FTMS data and app control".

### Waveshare 7" and 5" boards

Both are build targets. The 7" (`esp32-s3-devkitc-1`) was the first panel.
The 5" (`esp32-s3-lcd5`) is the one mounted on the Kuipers, chosen for size
and the onboard battery. The assessment below is the original 7" check.

Board:

- Waveshare ESP32-S3 7-inch capacitive touch display dev board
- 800x480 LCD, 5-point touch, Wi-Fi + BLE

Assessment for this rowing project:

- **Usability:** Very good for large, glanceable workout metrics from seated distance.
- **Performance headroom:** Good. ESP32-S3 + PSRAM class boards usually handle LVGL and BLE together if the UI is optimized.
- **Power:** Expect materially higher current draw than 3.2-3.5" SPI TFT builds. Use stable 5 V supply with margin.
- **Mechanical fit risk:** 7" may be large for the stock console mast and can interfere with handle movement if mounted too close.
- **Firmware complexity:** Higher than SPI TFT modules. These RGB touch boards often need vendor/BSP-specific display+touch init.
- **Environment risk:** Large glossy panels can be harder to read in bright light and need sweat splash protection.

Original recommendation, since followed (the 7" was bought; the 5" followed for size):

- **Buy** for a premium, easy-to-read on-device console, accepting the extra integration effort.
- A 3.5" SPI TFT would have been the faster first prototype. That path was not taken.

Current project note:

- Power supply budget is not a constraint, so the higher panel current draw is acceptable.

Original pre-buy checklist, satisfied by both SKUs:

- Confirm the exact Waveshare SKU has:
     - ESP32-S3 variant with enough PSRAM for your UI plan.
     - Public examples for your preferred stack (Arduino or ESP-IDF + LVGL).
     - Exposed GPIO suitable for Hall sensor input.
     - Known touch controller support in examples (for calibration/settings UI later).
- Plan a 5 V supply budget for display backlight + ESP32 peak load.
- Dry-fit cardboard dimensions at the console mast before final mount design.

---

## Firmware responsibilities (ESP32)

All of these are in `firmware/src` (map in `firmware/README.md`):

1. **Pulse capture** — GPIO interrupt on the 7" board; polled CH422G digital input on the 5".
2. **RPM** — from pulse period and pulses per wheel turn.
3. **Stroke detection** — flywheel speed rises on the drive and decays on the recovery. Settings can instead treat each pulse as one stroke.
4. **Session metrics** — time, strokes, rate, distance, split, watts, averages, calories. Pause freezes the session.
5. **Local UI** — home screen, Settings, and the session screen over the stern scene.
6. **FTMS** — Indoor Rower Data at 5 Hz (two notifications per update) and control-point Start / Stop / Pause / Reset.
7. **Session file** — TCX on the SD card when the session ends.
8. **Clock** — RTC on the 5" board; NTP over Wi-Fi when a network is saved.

### Calibration

Stock and DIY air-rower meters are not Concept2-accurate by default.

- Treat distance/split as **internally consistent** first.
- Tune against known pieces (timed intervals, perceived 2k effort, etc.).

### On-device settings (implemented)

Home screen → **SETTINGS** opens a menu of six tiles, each showing its
current value: Machine, Stroke detection, Sensor, Workout, Display, Clock &
Wi-Fi. Each page fits without scrolling; BACK returns to the menu, SAVE
saves everything, DEFAULTS shows on the machine/stroke/sensor pages only. Picking a machine or sensor type loads its
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

**Calibration** is the per-machine table in `firmware/include/machine_presets.h`
(overridable on the device; see "On-device settings"). The Kuipers preset
was tuned on 2026-10-15 against the original monitor: 354 m in 2:00, about
73 W at 29 spm, 1 pulse per wheel turn, C = 2.80. Inertia 1.9 is chosen so
the fitted recovery slope lands near k = 0.026. It is not a measured moment
of inertia. Dunlop inertia (0.10) and C (3.7) are still placeholders.

A synthetic unit test (`firmware/test/test_logic/test_main.cpp`) confirms
the OLS fit recovers a known drag factor. On the Kuipers, stroke count
matched a hand count on 2026-10-15.

### Session averages, calories, pause (implemented)

`MetricsModel` keeps session averages (pace = elapsed time / distance, rate =
strokes / elapsed minutes) and energy with Concept2's estimate, kcal/h =
4 × 0.8604 × W + 300, accumulated while rowing. `pause()`/`resume()` freeze
the session; elapsed time skips the pause. Shown on screen as "avg m:ss | N
spm" under the split and a KCAL tile.

### FTMS data and app control (implemented)

Indoor Rower Data goes out as two 12-byte notifications every 200 ms (the
first with "More Data" set), so it fits the 20-byte payload of the default
ATT MTU: part 0 = average rate, distance, pace, average pace, power; part 1 =
rate, stroke count, total energy / per hour / per minute, elapsed time.
Feature bit 9 (expended energy) is set. Control Point commands now act on
the session (`main.cpp`): Start opens the session screen and starts a
session (or resumes a paused one), Stop/Pause with parameter 1 stops (and
saves, below) or 2 pauses, Reset zeroes the session. A StopOrPause without
a parameter is answered InvalidParameter. Every change, from the app or the
display, is reported on Fitness Machine Status (0x2ADA): 0x01 reset, 0x02
stopped/paused + parameter, 0x04 started/resumed. RequestControl isn't
enforced (compatibility over spec purity, as before). The device name goes in the scan
response (the advertisement is full); before that, scanners saw an unnamed
FTMS device. Verified 2026-10-23 from a Mac with a bleak client acting as an
app: all control point commands answered and acted on, status
notifications sent, 468/468 data packets well-formed during a simulated
25-stroke session.

### Session files on the SD card (implemented)

`session_log.cpp` samples the session once per second (PSRAM, up to 4 h)
and, when it ends (the X button, serial `E`, or an app's Stop), writes
`/sessions/<local date>_<time>.tcx` on the card: one lap with time, distance,
calories, average cadence and average watts, and a trackpoint per second
(distance, cadence, watts). Sport is "Other" (TCX has no rowing); set the
type after uploading. Sessions without a stroke aren't written. When an app
or `E` ends the session, the session screen shows "STOPPED" and "saved
\<file\>", "no SD card, not saved", or "saving failed". The on-screen X
returns to the home screen before that banner is drawn, so the save result
is in the serial log.

Card access at run time (`sd_card.cpp`) on the 5" board pauses DI polling
and puts the CH422G in output mode for the mount (DI latches low first;
`sensor_input::takeExpanderForOutput()`), then back to input. The poll task
re-checks the pause flag after taking the I2C lock so it never reads the
output latch as a pulse. Measured: ~1.9 s mount, ~5 s for the whole save
(FAT32 free-space scan on first write), polling resumes at the normal rate
with no I2C errors.

**Clock:** the 5" board's PCF85063 RTC (I2C 0x51, CR927 cell) is read at
boot (`rtc_clock.cpp`; seconds bit 7 = oscillator stopped = not set; years
from 2000). Set it once with `scripts/set_clock.py --port …`. Without a set
clock files are named `session-NNN.tcx` and timestamped from 2000-01-01. The
7" board has no RTC known to this firmware: set the clock after each boot.

**USB serial commands** (one per line): `T<unix seconds>` sets the clock,
`S`/`E` start/end a session, `P` is one sensor pulse, `W` scans Wi-Fi,
`Y` syncs the clock with the saved network.
`scripts/sim_row.py --port … --strokes 20` rows a simulated session with
them, for testing at a desk.

### Pace boat (implemented)

Settings → WORKOUT: a Pace boat on/off switch and its Target split (default
2:30, 5 s per tap, hold to repeat; kept while the boat is off). NVS v4
appends `paceBoatOn`; v2/v3 blobs load as a prefix. During a session a translucent
single sculler (`pace_boat.cpp`) rows that split in a lane 2.5 m to the
right, drawn 6 m further ahead than it really is (a boat truly level with
ours is outside the camera's view: tried 4.5 m and 3 m lanes, only oar tips
showed), so level = just past our bow, positioned with the scene camera (`rower_scene::project()`) and drawn
over each frame in the present buffer. Its oars swing at 24 spm. It starts
level when the session clock starts and drops out of view about 6 m
behind. The label under the top bar reads "PACE 2:30 +12 m" (green: you're
ahead; orange: behind).

### Clock over Wi-Fi, time zone (implemented)

Settings → Clock & Wi-Fi: a time zone (POSIX TZ strings with DST rules,
`firmware/include/timezones.h`; settings store the index, append only) and
a Wi-Fi network picked from a scan, password typed on an on-screen
keyboard (`wifi_screen.cpp`). `net_time.cpp` runs each job on its own task
and turns Wi-Fi off afterwards: at boot and after the network changes it
connects, syncs NTP (`configTzTime`, not `configTime`, which overwrites
TZ), writes the RTC and disconnects; SYNC does it on demand. The radio is
off the rest of the time because RF and flash activity disturb the RGB
panel; `resyncDisplay()` runs after each job (Wi-Fi start can write PHY
calibration to NVS). Credentials live in our settings blob (NVS v5),
`WiFi.persistent(false)`. RTC and system clock hold UTC (`rtc_clock` reads
the RTC with its own date maths, since `mktime()` applies the zone); the
zone only affects local time: the home screen clock and session file
names. TCX times stay UTC. Wi-Fi adds ~550 KB flash (61.5%) and ~25 KB
static RAM; ~71 KB internal heap free after boot. Serial `W` scans and
logs the networks, `Y` syncs with the saved settings.

Lessons from bring-up (2026-10-23): the status shown as "status 0, reason
0" was a station that had *joined* (STA_CONNECTED) but had no IP yet;
Arduino only reports WL_CONNECTED after DHCP, so the message now says
"joined, but got no IP address" in that case and the timeout is 30 s.
`sntp_get_sync_status()` returns COMPLETED once and then resets, so read it
once. While the radio is on, the panel is re-aligned every 500 ms
(`waitMs()` in `net_time.cpp`): Wi-Fi's PSRAM buffers
(`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` in the prebuilt Arduino IDF)
compete with the RGB frame DMA and visibly shifted the picture. Measured:
join ~3 s, IP 13 ms later, NTP ~0.4 s, radio off ~3.5 s after it started. DHCP failed in
about half the attempts until `WiFi.setSleep(false)` (BLE shares the radio;
the offer was missed in modem sleep): 6/6 syncs in 2–6 s after.

**Splash screen and boot order:** Wi-Fi and the scene-frame rendering
visibly disturbed the RGB panel when they ran together, so they never do.
`panel_runtime::begin()` brings up only a splash screen; `main.cpp` then
runs the clock sync to the end (if due) with its progress on the splash,
and only then calls `panel_runtime::buildScreens()` (SD backgrounds, photo
conversion, frame rendering, splash says "Preparing the scene..."). Later
syncs (weekly check, SYNC, a new network) also block with the splash up
and return to the previous screen; the scene animation is paused behind
it. Never mid-session. Serial `F` forgets the last sync so the next boot
syncs. Measured boot with a due sync: clock set ~7 s after power-on, then
frames in ~7.2 s.

**Schedule:** weekly. At boot only if the clock was last set ≥ 7 days ago
(or isn't valid), and an hourly check while the display stays on, never
during a session. Wi-Fi changes and SYNC sync at once. The last-set time is
an NVS key (`clockSet`), written by every clock set.

**Clock battery:** its voltage can't be read: BAT1 (CR927) only feeds the
RTC's VDD through D15/D16, and the PCF85063A has no battery-low flag. What
is detectable is lost time: if the RTC's oscillator-stopped flag is set at
boot although the clock had been set before, the board was fully powered
off (no USB, LiPo switched off) without a working cell. Then the home
screen clock shows "clock battery?" and the Clock & Wi-Fi page says so.

### On-device UI (implemented)

`firmware/src/panel_runtime.cpp` draws two LVGL screens.

The home screen is the title "Let's get rowing!", the machine and sensor,
START SESSION, SETTINGS, and the clock (5" board, when the RTC is set).

The session screen is the precomputed stern scene (`rower_scene`: the photo
background, the rower, and the water, blitted at 800×480) with tiles over
it. Top band: SPLIT /500m, the session average split and rate, SPM, WATTS.
Bottom band: TIME, STROKES, METERS, KCAL. With the pace boat on, a label
under the top band shows the target split and the gap in metres. The X
ends the session and returns home. An app can also pause the session; the
banner then reads PAUSED. There is no pause button on the panel.

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

---

## Phone apps

FTMS Indoor Rower is the phone link. See "FTMS data and app control" for
the packet split, the energy fields, and the control point. A phone is not
required for a session.

There is no companion app in this repo. History is the TCX file on the SD
card. The target that exists on the device is the pace boat. Calibration
is the Settings screen. Apps that speak FTMS can read the rower data and
send Start, Stop, Pause, and Reset. That path was checked on 2026-10-23
from a Mac with a bleak client, not yet against a particular training app.

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
| Machines | Dunlop Air Rower 750 and Kuipers AquaRower, selectable in Settings. Daily use is the Kuipers |
| Sensor | Hall on both machines, one magnet, 3.3 V open-drain. Kuipers still on its stock reed until the mount is checked on the machine. Dunlop preset uncalibrated |
| Stock monitor | Replaced on both |
| MCU and display | ESP32-S3, Waveshare 5" (daily) and 7", both 800×480 touch |
| Phone required? | No. FTMS when a phone is present |
| Power | USB-C, or the board's LiPo. No battery gauge |
| Clock | 5" RTC, plus NTP when a Wi-Fi network is saved |

## Still open

- Kuipers Hall mount, on the machine: a third wire up the mast, the gap from the reed clip to the magnet, which pole faces the clip, and whether that pocket stays dry. The 5" `DI1` input accepts a 3.3 V open-drain Hall; a push-pull part and a classic A3144 (about 4.5 V minimum) do not match the board as it sits.
- Dunlop inertia and boat constant, against a known distance.
- Battery voltage. Both boards need a hardware modification before the firmware can read it.
- A training app (Kinomap, EXR, or similar) against the FTMS service. The bleak check covered the packets and the control point. A saved TCX passes Garmin's schema; uploading one to Strava, Intervals.icu, or Garmin Connect is still to do.

The original bring-up list (pulse log, on-screen metrics, brackets, distance model, BLE) is done for the Kuipers and the 5" board. Pin map, panel size, and the drag-factor distance model are in the sections above.

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
| 2026-10-23 | Docs caught up with the firmware: RTC, scene, FTMS, TCX, pace boat, Wi-Fi clock, and the 5" reed path are implemented. The Dunlop preset and a Hall swap on the Kuipers are still open. |
| 2026-10-23 | Sensor plan is a Hall on both machines. Kuipers feasibility is the mast cable, the magnet gap, the pole, and splash; the 5" input wants a 3.3 V open-drain part. |
