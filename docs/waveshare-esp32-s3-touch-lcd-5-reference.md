# Waveshare ESP32-S3 Touch LCD 5 reference

Pin and connector notes for the 5" board, the console in daily use
(`esp32-s3-lcd5`). The vendor repo is the source for the pin mapping and
the init patterns. How this firmware uses each connector is in
[Hardware.md](../Hardware.md).

## Official resources

- Product page: https://www.waveshare.com/esp32-s3-touch-lcd-5.htm
- Wiki: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-5
- Manufacturer repo: https://github.com/waveshareteam/ESP32-S3-Touch-LCD-5
- Docs: https://docs.waveshare.com/ESP32-S3-Touch-LCD-5

## Key board facts

- SoC: ESP32-S3
- Display: 5-inch RGB LCD, 800x480 (a 1024x600 SKU variant also exists)
- Controller: ST7262
- Touch: GT911 over I2C
- Touch I2C: SDA=8, SCL=9, INT=4
- Panel RGB bus uses the same Waveshare board pin mapping as the 7" board
  below (`esp_panel_board_supported_conf.h`'s `BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_5`
  and `_LCD_7` configs are byte-identical apart from the board name/SKU string)

### RGB data pins

- D0 = 14
- D1 = 38
- D2 = 18
- D3 = 17
- D4 = 10
- D5 = 39
- D6 = 0
- D7 = 45
- D8 = 48
- D9 = 47
- D10 = 21
- D11 = 1
- D12 = 2
- D13 = 42
- D14 = 41
- D15 = 40

### RGB control pins

- HSYNC = 46
- VSYNC = 3
- DE = 5
- PCLK = 7
- DISP = -1

### Touch bus

- I2C host: 0
- SDA: 8
- SCL: 9
- Interrupt: 4

## IO expander (CH422G) — the real 5"-vs-7" difference

Unlike the 7" board, this board has **no raw-GPIO general sensor header**
("Sensor AD"). Several board functions, plus a terminal-block isolated I/O
interface, are instead multiplexed behind a single CH422G I2C IO expander
(address 0x20, same I2C bus as touch):

| Function | CH422G pin |
|---|---|
| LCD reset | IO1 |
| Backlight enable | IO2 |
| Touch reset | IO3 |
| SD/TF card CS | IO4 (SPI data pins are raw GPIO: MOSI=11, SCK=12, MISO=13) |
| `DI0` terminal | IO0 |
| `DI1` terminal | IO5 |
| `DO0` terminal | OC0 |
| `DO1` terminal | OC1 |

CH422G's `IO0-IO7` pins share **one** global direction bit (all-input or
all-output — see the vendor driver's `WR_SET`/`IO_OE` bit, bundled at
`firmware/lib/ESP32_IO_Expander/src/port/esp_io_expander_ch422g.c`). `RD_IO`
only reflects the real pin levels in input mode; in output mode it reads
back the latch. Board init leaves IO0-7 as outputs, so
`firmware/src/sensor_input.cpp` calls `enableAllIO_Input()` after the
display is up and polls `DI1` (EXIO5) by default. SD card access borrows
the expander back to output mode, because chip select is EXIO4, and
returns it to input afterwards. Reed wiring is in Hardware.md: `DI1` to
`GND`, `DI_COM` left unconnected.

### Terminal block (16-way screw terminal)

```
VIN GND VOUT GND | SDA SCL | CAN_L CAN_H | 485_B 485_A | DO0 DO1 DI_COM GND DI0 DI1
```

- `DI0`/`DI1`: isolated digital inputs (see the CH422G table above). The
  board powers `DI_COM`. A dry contact goes from `DIx` to `GND`. Bridging
  `DIx` to `DI_COM` does not trigger the input. The firmware default is
  `DI1`.
- `DO0`/`DO1`: CH422G open-collector outputs (OC0/OC1).
- `CAN_L`/`CAN_H`: TWAI, shares GPIO19/20 with native USB.
- `485_A`/`485_B`: RS485, GPIO15/16.
- `SDA`/`SCL`: general-purpose I2C breakout (same bus as touch/expander,
  GPIO8/9) — not a separate bus.
- `VIN`/`VOUT`: board power input/regulated output rail.

### Other connectors

- RTC: CR927 coin-cell holder, backs a PCF85063. `rtc_clock.cpp` reads it
  at boot (I2C 0x51). The cell voltage is not on a GPIO.
- Battery: separate 2-pin JST `Battery` connector + `Battery Switch` +
  onboard LiPo charge circuit (`CHG`/`DONE` LEDs), distinct from the RTC
  coin cell. The installed board runs on a LiPo here and charges over
  USB-C. Charge status is the LEDs only; the firmware cannot read pack
  voltage.
- TF/SD card slot: top-left corner, SPI as above. Used for scene
  backgrounds and session TCX files.

CAN, RS485, and `DO0`/`DO1` are unused by this firmware. The reed uses
`DI1` (Settings can select `DI0`).

## Notes

- The BOOT button is used for download/programming mode.
- This reference is intentionally not treated as the primary implementation source for the rowing display app.
