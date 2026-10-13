# Waveshare ESP32-S3 Touch LCD 5 reference

This board is kept as a reference only for the current project. We are not using the vendor repo as the active firmware source; we are using it to validate the correct board pin mapping and initialization patterns.

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
`firmware/lib/ESP32_IO_Expander/src/port/esp_io_expander_ch422g.c`). Since
IO1-IO4 above must stay in output mode for the board to keep functioning
(backlight, resets, SD-CS), `DI0`/`DI1` can never be switched into a true
input mode without breaking those. They're usable anyway because the pins
are quasi-bidirectional: left at their output latch's power-on default
(logic 1, weak pull), an external contact pulling the line low is still
visible on a plain I2C read (`digitalRead()`/`read_input_reg()`, independent
of the direction register). This project's `firmware/src/sensor_input.cpp`
polls `DI0` this way for the stroke/reed sensor — see that file and
`Hardware.md`'s "Sensor wiring differs by board" section for the reed
wiring and the project-specific implementation.

### Terminal block (16-way screw terminal)

```
VIN GND VOUT GND | SDA SCL | CAN_L CAN_H | 485_B 485_A | DO0 DO1 DI_COM GND DI0 DI1
```

- `DI0`/`DI1`: isolated digital inputs (see CH422G table above). `DI_COM`
  floating = dry-contact/passive mode (plain switch/reed, no external
  power); tied to supply +/- = NPN/PNP wet-contact active mode, 5-36V DC.
- `DO0`/`DO1`: CH422G open-collector outputs (OC0/OC1).
- `CAN_L`/`CAN_H`: TWAI, shares GPIO19/20 with native USB.
- `485_A`/`485_B`: RS485, GPIO15/16.
- `SDA`/`SCL`: general-purpose I2C breakout (same bus as touch/expander,
  GPIO8/9) — not a separate bus.
- `VIN`/`VOUT`: board power input/regulated output rail.

### Other connectors

- RTC: CR927 coin-cell holder, backs a PCF85063 RTC chip.
- Battery: separate 2-pin JST `Battery` connector + `Battery Switch` +
  onboard LiPo charge circuit (`CHG`/`DONE` LEDs) — distinct from the RTC
  coin cell, not used in a USB-C-powered install.
- TF/SD card slot: top-left corner, SPI as above.

None of RTC, battery, SD, CAN, RS485, DO0/DO1 are wired up by this
project's firmware — DI0 (the reed sensor) is the only terminal in active
use.

## Notes

- The BOOT button is used for download/programming mode.
- This reference is intentionally not treated as the primary implementation source for the rowing display app.
