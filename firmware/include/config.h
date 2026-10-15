#pragma once

#include <cstdint>

// Machine/sensor parameters that differ per rower (pulses per turn,
// inertia, drag seed, boat constant, stroke-phase timing, debounce) live in
// machine_presets.h as factory defaults and are user-editable at runtime
// (settings.h). This file holds the fixed board/algorithm constants.
#include "machine_presets.h"

namespace cfg {

// Sensor input source differs by board:
//
// - 7"/devkit (default): GPIO6 is the one pin this board's own examples
//   (04_Sensor_AD) expose for a general external sensor, wired straight to
//   the "Sensor AD" 3-pin header (3V3/GND/AD). Every other low GPIO is
//   already claimed onboard: LCD data/timing (0,1,2,3,5,7,10,14,17,18,21,
//   38-48), touch+expander I2C (8,9,4), SD card SPI (11,12,13), RS485
//   (15,16), and native USB/TWAI (19,20). GPIO16 in particular is the RS485
//   transceiver's TX line — do not use it here. A raw pin, so sensing it
//   uses a hardware CHANGE interrupt (see sensor_input.cpp).
//
// - 5" board: has no equivalent raw-GPIO sensor header. Its terminal block's
//   DI0/DI1 are read through the CH422G IO expander over I2C (same chip the
//   display/touch/SD-CS already use), not a direct ESP32 pin, so there's no
//   hardware interrupt available for them — sensor_input.cpp polls instead.
//   CH422G's IO0-7 share one direction bit, and RD_IO only reflects the real
//   pins in input mode, so sensor_input.cpp switches all of IO0-7 to input
//   after the display is up (as Waveshare's 05_IO_Test does).
#if defined(ROWING_BOARD_LCD_5)
// DI0 = EXIO0, DI1 = EXIO5 (Waveshare 05_IO_Test). Each DI terminal is a
// PC814 opto input: contact between DIx and GND turns it on -> reads LOW.
// Default terminal; selectable in Settings.
constexpr uint8_t DI0_EXPANDER_PIN = 0;
constexpr uint8_t DI1_EXPANDER_PIN = 5;
constexpr uint8_t DEFAULT_EXPANDER_PIN = DI1_EXPANDER_PIN;
#else
constexpr uint8_t HALL_PIN = 6;
#endif

constexpr uint32_t SERIAL_BAUD = 115200;

// Stroke mode tuning. Pulses closer than MIN_STROKE_INTERVAL_MS are switch
// bounce, not strokes (250 ms = 240 spm, far above any real rate).
constexpr uint32_t MIN_STROKE_INTERVAL_MS = 250;
constexpr float METERS_PER_STROKE = 10.0f;
// Fastest plausible boat speed (~1:23 /500m). Rapid tapping would otherwise
// turn a fixed metres-per-stroke into absurd split/watts figures.
constexpr float MAX_BOAT_SPEED_MPS = 6.0f;
// Share of the stroke cycle spent on the drive (rest is recovery), and the
// cycle assumed for the very first stroke before an interval is measured.
constexpr float DRIVE_FRACTION = 0.4f;
constexpr uint32_t FIRST_STROKE_INTERVAL_MS = 2000;

// Smoothing and cadence behavior tuning
constexpr float OMEGA_ALPHA = 0.2f;
constexpr uint32_t STROKE_IDLE_TIMEOUT_MS = 2500;

// Metrics update cadence
constexpr uint32_t LOOP_INTERVAL_MS = 50;
constexpr uint32_t FTMS_NOTIFY_INTERVAL_MS = 200;

// Recovery-phase regression: need at least this many (time, 1/omega) points
// to attempt a fit, and the fit's R^2 must clear this bar to be accepted.
// General algorithm robustness tuning, not machine-specific physics, so
// it's shared across rower profiles.
constexpr int MIN_REGRESSION_POINTS = 4;
constexpr float MIN_FIT_R2 = 0.7f;
// How much a newly accepted per-stroke drag fit moves the running value;
// drag is near-constant stroke to stroke, so this is deliberately slow.
constexpr float DRAG_FACTOR_ALPHA = 0.25f;

} // namespace cfg
