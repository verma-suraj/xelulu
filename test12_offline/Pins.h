// ============================================================
// Xelulu — the ONLY file where GPIO numbers appear (FW-SOT §2.4,
// as wired in Xelulu_Wiring.md). Board: ESP32-S3-DevKitC-1 N16R8.
// As built 2026-10-24 (servo bus on the native USB pins), mics INMP441 since 2026-11-01.
//
// One forced exception: TFT_eSPI reads its pins at compile time, so
// build_opt.h repeats the display pins as -D flags. face/Face.cpp
// static_asserts that the two copies agree.
// Never use GPIO 26-37 (octal flash + PSRAM on N16R8). 38-40: the built-in SD slot (unused).
// 43/44: the DevKit's USB-serial chip (dead on this board, leave free).
// ============================================================
#pragma once

// ---- I2C0, 400 kHz, owned by the Sense task only ----
// MPR121 0x5A · MPU6050 0x68 · BMP180 0x77 · TCA9548A 0x70 (2x VL53L0X 0x29 behind it, CH0/CH1)
#define PIN_I2C_SDA        8
#define PIN_I2C_SCL        9

// ---- I2S0 RX: 2x INMP441 on one SD line (L/R GND = left, 3.3 V = right; 100k SD pull-down) ----
#define PIN_MIC_BCLK       4   // SCK
#define PIN_MIC_WS         5
#define PIN_MIC_DATA       6   // SD

// ---- I2S1 TX: MAX98357A (SD on 3.3 V = always enabled, GAIN floating = 9 dB) ----
#define PIN_SPK_BCLK      15
#define PIN_SPK_LRC       16
#define PIN_SPK_DIN        7

// ---- SPI3 (USE_HSPI_PORT, build_opt.h): the TFT ----
#define PIN_SPI_SCLK      12
#define PIN_SPI_MOSI      11
#define PIN_SPI_MISO      -1   // none: the TFT is write-only, and GPIO13 is TB6612 BIN1 as built
#define PIN_TFT_CS        10
#define PIN_TFT_DC        14
#define PIN_TFT_RST       21
#define PIN_TFT_EN        45   // backlight, HIGH = on. A strapping pin: no pull-up on it

// ---- Servo bus: SCS protocol, 1 Mbps, TX -> 1 kOhm -> signal <- RX ----
// These are the native USB pins (D+ 20, D- 19): with a USB host on that port at boot the
// console wins and the bus stays off (body/Body.cpp scanBus). No fallback pair on this build.
#define PIN_SERVO_TX      20
#define PIN_SERVO_RX      19

// ---- TB6612FNG. STBY is tied to 3.3 V, so the IN pins are the only stop ----
#define PIN_MOT_L_PWM      1
#define PIN_MOT_L_IN1      2
#define PIN_MOT_L_IN2     42
#define PIN_MOT_R_PWM     41
#define PIN_MOT_R_IN1     13
#define PIN_MOT_R_IN2     47

// ---- N20 encoders: PCNT rising edges on C1, direction from the command ----
// ponytail: C2 (left 48, right 3) is wired for quadrature but unread; add it if a wheel is ever pushed backwards
#define PIN_ENC_L         17
#define PIN_ENC_R         18
