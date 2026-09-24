# Waveshare ESP32-S3 Touch LCD 7 reference

This board is kept as a reference only for the current project. We are not using the vendor repo as the active firmware source; we are using it to validate the correct board pin mapping and initialization patterns.

## Official resources

- Product page: https://www.waveshare.com/esp32-s3-touch-lcd-7.htm
- Wiki: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7
- Manufacturer repo: https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7
- Docs: https://docs.waveshare.net/ESP32-S3-Touch-LCD-7

## Key board facts

- SoC: ESP32-S3
- Display: 7-inch RGB LCD, 800x480
- Controller: ST7262
- Touch: GT911 over I2C
- Touch I2C: SDA=8, SCL=9, INT=4
- Panel RGB bus uses the Waveshare board pin mapping below

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

## Notes

- The BOOT button is used for download/programming mode.
- The factory demo confirms the display and touch hardware are operational.
- This reference is intentionally not treated as the primary implementation source for the rowing display app.
