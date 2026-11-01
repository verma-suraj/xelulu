// ============================================================
// Eilik_Bot — single source of truth for all wiring.
// Board: ESP32 Dev Module (WROOM-32, 30-pin DevKit)
// ============================================================
#ifndef EILIK_PINS_H
#define EILIK_PINS_H

// ---- Display: SmartElex 1.28" round GC9A01, 240x240 (VSPI) ----
// These MUST match your TFT_eSPI User_Setup.h (see README).
#define PIN_TFT_SCLK   18   // GC9A01 SCL
#define PIN_TFT_MOSI   23   // GC9A01 SDA
#define PIN_TFT_CS      5   // GC9A01 CS
#define PIN_TFT_DC     13   // GC9A01 DC (NOT GPIO2: display pull-up on the
                            // boot-strap pin blocked uploads while connected)
#define PIN_TFT_RST     4   // GC9A01 RST
// GC9A01 BLK/backlight -> 3V3

// ---- Touch controller on the display: CST816S (I2C) ----
#define PIN_I2C_SDA    21   // shared bus: CST816S (0x15) + MPU6050 (0x68)
#define PIN_I2C_SCL    22
#define PIN_CTP_RST    33   // CST816S RST
#define PIN_CTP_INT    27   // CST816S INT

// ---- MPU6050 (shares I2C bus above; AD0 -> GND, INT not connected) ----

// ---- Audio: MAX98357A I2S amp -> 4ohm 3W speaker ----
#define PIN_I2S_BCLK   26
#define PIN_I2S_LRC    25   // LRC / WS
#define PIN_I2S_DIN    19
// MAX98357A GAIN: leave floating = 9 dB. SD: leave as breakout default.

// ---- Servos: MG90S 360 continuous rotation, 50 Hz PWM ----
// Powered from the buck 5V rail, NOT from the ESP32 3V3/5V pin.
#define PIN_SERVO_ARM_L  16
#define PIN_SERVO_ARM_R  17
#define PIN_SERVO_HEAD   14   // brief glitch pulse at boot possible (strap pin) — harmless twitch
#define PIN_SERVO_TORSO  15   // same note as GPIO14

// ---- ESP32 capacitive touch inputs (bare wire / copper pad) ----
#define PIN_TOUCH_HEAD   32   // T9 — pad on top of head
#define PIN_TOUCH_BODY    2   // T2 — pad on belly/back (floating pad wire is
                              // boot-strap safe; swapped with TFT_DC on 13)

// ---- Calibration knobs (hardware never matches paper) ----
// Continuous-rotation MG90S: 1500us should be "stopped", but each unit
// drifts. Tune per servo until it holds still, range roughly -60..+60 us.
#define SERVO_STOP_TRIM_ARM_L   0
#define SERVO_STOP_TRIM_ARM_R   0
#define SERVO_STOP_TRIM_HEAD    0
#define SERVO_STOP_TRIM_TORSO   0
#define SERVO_SPEED_RANGE_US  400  // us offset from stop at speed 100

#define TOUCH_THRESH_PCT       60  // touched when reading < 60% of boot baseline

#define SHAKE_SPIKE_G        0.55f // accel delta (g) counted as a shake spike
#define KNOCK_MIN_G          0.20f // desk-knock detection window (g)
#define TILT_DEG               45  // held/tilted beyond this angle

#endif
