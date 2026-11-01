// test8's pins, as built (Xelulu_Wiring.md): only what the face, speaker and touch use.
// xelulu_fw/Pins.h is stale (servo 43/44, MISO on 13 = now a motor pin), so this sketch has its own.
#pragma once

#define PIN_I2C_SDA        8
#define PIN_I2C_SCL        9
#define PIN_SPK_BCLK      15
#define PIN_SPK_LRC       16
#define PIN_SPK_DIN        7
#define PIN_SPI_SCLK      12
#define PIN_SPI_MOSI      11
#define PIN_SPI_MISO      -1   // none: the TFT is write-only and GPIO13 is TB6612 BIN1
#define PIN_TFT_CS        10
#define PIN_TFT_DC        14
#define PIN_TFT_RST       21
#define PIN_TFT_EN        45   // backlight, HIGH = on, ~0.5 s after power-up
