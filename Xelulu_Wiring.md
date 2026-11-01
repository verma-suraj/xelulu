# Xelulu — Wiring Tables

Brain = **ESP32‑S3 (N16R8)**. No external microSD module — using the board's built‑in SD slot (needs no wires).

Read each row as: **this pin on Module A → that pin on Module B**.
"5V", "3.3V", "GND" = the power rails from the Power table (§1).

> Build order: wire §1 Power, check 5.0 V and 3.3 V with a multimeter, **then** connect the ESP, then one module at a time.

---

## 1. Power

| Module A | Pin A | → Module B | Pin B | Note |
|---|---|---|---|---|
| 18650 cell 1 | − | 18650 cell 2 | + | cells in **series** (2S) |
| 18650 pack | cell‑2 − | 2S BMS | B− | |
| 18650 pack | cell‑1 − / cell‑2 + junction | 2S BMS | BM | middle tap |
| 18650 pack | cell‑1 + | 2S BMS | B+ | |
| 2S BMS | P+ | KCD1 switch | pin 1 | |
| KCD1 switch | pin 2 | XY‑3606 buck | IN+ | = **7.4V** line |
| 2S BMS | P− | XY‑3606 buck | IN− | = **GND** |
| XY‑3606 buck | OUT+ | **5V rail** | — | set pot to 5.0 V **before** loading |
| XY‑3606 buck | OUT− | **GND rail** | — | |
| 5V rail | — | AMS1117 | IN | |
| GND rail | — | AMS1117 | GND | |
| AMS1117 | OUT | **3.3V rail** | — | |
| Type‑C breakout | VBUS | 1794301 charger | IN+ | |
| Type‑C breakout | GND | 1794301 charger | IN− | |
| 1794301 charger | B+ | 2S BMS | B+ | charging |
| 1794301 charger | B− | 2S BMS | B− | charging |

All GNDs connect together (one common ground).

---

## 2. ESP32‑S3 power

| Module A | Pin A | → Module B | Pin B |
|---|---|---|---|
| ESP32‑S3 | 5V | 5V rail | — |
| ESP32‑S3 | GND | GND rail | — |

---

## 3. Face display — 2" IPS ST7789 (SPI)

| Module A | Pin A | → Module B | Pin B |
|---|---|---|---|
| Display | VCC | 3.3V rail | — |
| Display | GND | GND rail | — |
| Display | SCL | ESP32‑S3 | GPIO12 |
| Display | SDA | ESP32‑S3 | GPIO11 |
| Display | RST | ESP32‑S3 | GPIO21 |
| Display | DC | ESP32‑S3 | GPIO14 |
| Display | CS | ESP32‑S3 | GPIO10 |
| Display | EN | ESP32‑S3 | GPIO45 |
| Display | TE | (not connected) | — |

EN = backlight on: the firmware sets GPIO45 HIGH about 0.5 s after power‑up (as `Display_test_code` does).
GPIO45 is a boot strapping pin and must be LOW at reset, so don't add a pull‑up on EN.
TE is an optional sync output — not needed.

---

## 4. Microphones — 2× GY‑SPH0645 (I2S)

| Module A | Pin A | → Module B | Pin B |
|---|---|---|---|
| Mic LEFT | 3V | 3.3V rail | — |
| Mic LEFT | GND | GND rail | — |
| Mic LEFT | BCLK | ESP32‑S3 | GPIO4 |
| Mic LEFT | LRCL | ESP32‑S3 | GPIO5 |
| Mic LEFT | DOUT | ESP32‑S3 | GPIO6 |
| Mic LEFT | SEL | GND rail | — |
| Mic RIGHT | 3V | 3.3V rail | — |
| Mic RIGHT | GND | GND rail | — |
| Mic RIGHT | BCLK | ESP32‑S3 | GPIO4 |
| Mic RIGHT | LRCL | ESP32‑S3 | GPIO5 |
| Mic RIGHT | DOUT | ESP32‑S3 | GPIO6 |
| Mic RIGHT | SEL | 3.3V rail | — |

Both mics share GPIO4/5/6. SEL is the only difference (GND = left, 3.3V = right).

---

## 5. Speaker amp — MAX98357 (I2S)

| Module A | Pin A | → Module B | Pin B |
|---|---|---|---|
| MAX98357 | VIN | 5V rail | — |
| MAX98357 | GND | GND rail | — |
| MAX98357 | BCLK | ESP32‑S3 | GPIO15 |
| MAX98357 | LRC | ESP32‑S3 | GPIO16 |
| MAX98357 | DIN | ESP32‑S3 | GPIO7 |
| MAX98357 | SD | 3.3V rail | — |
| MAX98357 | GAIN | (not connected) | — |
| MAX98357 | + | Speaker 8Ω | + |
| MAX98357 | − | Speaker 8Ω | − |

---

## 6. Touch — MPR121 (I2C)

| Module A | Pin A | → Module B | Pin B |
|---|---|---|---|
| MPR121 | VIN | 3.3V rail | — |
| MPR121 | GND | GND rail | — |
| MPR121 | SDA | ESP32‑S3 | GPIO8 |
| MPR121 | SCL | ESP32‑S3 | GPIO9 |
| MPR121 | ADDR | GND rail | — |
| MPR121 | ELE0 | Touch pad | Head |
| MPR121 | ELE1 | Touch pad | Back |
| MPR121 | ELE2 | Touch pad | Belly |
| MPR121 | ELE3 | Touch pad | Left hand |
| MPR121 | ELE4 | Touch pad | Right hand |

---

## 7. IMU — GY‑87 (I2C)

| Module A | Pin A | → Module B | Pin B |
|---|---|---|---|
| GY‑87 | VCC | 3.3V rail | — |
| GY‑87 | GND | GND rail | — |
| GY‑87 | SDA | ESP32‑S3 | GPIO8 |
| GY‑87 | SCL | ESP32‑S3 | GPIO9 |

---

## 8. Distance sensors — TCA9548A mux + 2× VL53L0X V2 (I2C)

| Module A | Pin A | → Module B | Pin B |
|---|---|---|---|
| TCA9548A | VIN | 3.3V rail | — |
| TCA9548A | GND | GND rail | — |
| TCA9548A | SDA | ESP32‑S3 | GPIO8 |
| TCA9548A | SCL | ESP32‑S3 | GPIO9 |
| TCA9548A | A0, A1, A2 | GND rail | — |
| TCA9548A | RST | 3.3V rail | — |
| VL53L0X #1 (front‑left) | SDA / SCL | TCA9548A | SD0 / SC0 |
| VL53L0X #2 (front‑right) | SDA / SCL | TCA9548A | SD1 / SC1 |
| each VL53L0X | VIN | 3.3V rail | — |
| each VL53L0X | GND | GND rail | — |
| each VL53L0X | XSHUT, GPIO1 | not connected | — |

Both sensors look forward (obstacles); mux channels 2–7 stay empty. Both sit at 0x29; the mux keeps them apart, so XSHUT can float (the board pulls it up). If one doesn't answer, tie its XSHUT to 3.3V.
No cliff sensors: nothing stops the robot at a table edge.

---

## 9. Motors — TB6612 + 2× N20 encoder motors

| Module A | Pin A | → Module B | Pin B |
|---|---|---|---|
| TB6612 | VM | 5V rail | — |
| TB6612 | VCC | 3.3V rail | — |
| TB6612 | GND | GND rail | — |
| TB6612 | STBY | 3.3V rail | — |
| TB6612 | PWMA | ESP32‑S3 | GPIO1 |
| TB6612 | AIN1 | ESP32‑S3 | GPIO2 |
| TB6612 | AIN2 | ESP32‑S3 | GPIO42 |
| TB6612 | PWMB | ESP32‑S3 | GPIO41 |
| TB6612 | BIN1 | ESP32‑S3 | GPIO13 |
| TB6612 | BIN2 | ESP32‑S3 | GPIO47 |
| TB6612 | AO1 | Left motor | M+ |
| TB6612 | AO2 | Left motor | M− |
| TB6612 | BO1 | Right motor | M+ |
| TB6612 | BO2 | Right motor | M− |
| Left motor encoder | VCC | 3.3V rail | — |
| Left motor encoder | GND | GND rail | — |
| Left motor encoder | C1 | ESP32‑S3 | GPIO17 |
| Left motor encoder | C2 | ESP32‑S3 | GPIO48 |
| Right motor encoder | VCC | 3.3V rail | — |
| Right motor encoder | GND | GND rail | — |
| Right motor encoder | C1 | ESP32‑S3 | GPIO18 |
| Right motor encoder | C2 | ESP32‑S3 | GPIO3 |

C1 + C2 together give speed **and** direction. If a wheel counts backwards in software, swap that motor's C1/C2 in code (no rewiring).

---

## 10. Servos — 3× Waveshare SC09 (serial bus)

Each servo has 3 wires: **VIN** (red), **GND** (black/brown), **Signal** (the third one). All three servos connect the same way: same‑colour wires joined together.

The servo has **one** Signal wire (it listens and replies on it). The ESP has **two** pins: GPIO20 talks (TX), GPIO19 listens (RX). A 1 kΩ resistor lets both share the one wire:

```
GPIO20 (TX) ──[ 1 kΩ ]──┐
                        ├──── Signal of servo 1 + 2 + 3      ← "SIGNAL point"
GPIO19 (RX) ────────────┘
5V rail     ───────────────── VIN of servo 1 + 2 + 3
GND rail    ───────────────── GND of servo 1 + 2 + 3
```

| Module A | Pin A | → Module B | Pin B | Note |
|---|---|---|---|---|
| ESP32‑S3 | GPIO20 (TX) | 1 kΩ resistor | either leg | a resistor has no direction |
| 1 kΩ resistor | other leg | SIGNAL point | — | |
| ESP32‑S3 | GPIO19 (RX) | SIGNAL point | — | plain wire, no resistor |
| Servo 1, 2, 3 | Signal | SIGNAL point | — | |
| Servo 1, 2, 3 | VIN (red) | 5V rail | — | **5V only.** SC09 is rated 4–6 V; 7.4V can destroy it |
| Servo 1, 2, 3 | GND | GND rail | — | |

SIGNAL point = one spot where 5 wires meet (resistor leg, GPIO19, three servo Signal wires): a breadboard row, or a solder joint.
1 kΩ colour bands: brown‑black‑red (4‑band) or brown‑black‑black‑brown (5‑band).

GPIO19/20 are the board's native USB pins, so that USB port is out of use: **never plug a cable into it**.
GPIO43/44 stay free for the board's COM port (its USB‑serial chip), which does flashing and the console.
(The servo bus used to be on 43/44, but the USB‑serial chip drives GPIO44, so the COM port and the servos blocked each other.)

Set servo IDs once before assembly: **1 = head, 2 = left arm, 3 = right arm**.

---

## 11. ESP32‑S3 pin summary (quick check)

| GPIO | Goes to |
|---|---|
| 1 | TB6612 PWMA |
| 2 | TB6612 AIN1 |
| 3 | Right encoder C2 |
| 4 | Both mics BCLK |
| 5 | Both mics LRCL |
| 6 | Both mics DOUT |
| 7 | MAX98357 DIN |
| 8 | SDA — MPR121, GY‑87, TCA9548A |
| 9 | SCL — MPR121, GY‑87, TCA9548A |
| 10 | Display CS |
| 11 | Display MOSI |
| 12 | Display SCK |
| 13 | TB6612 BIN1 |
| 14 | Display DC |
| 15 | MAX98357 BCLK |
| 16 | MAX98357 LRC |
| 17 | Left encoder C1 |
| 18 | Right encoder C1 |
| 19 | Servo bus (direct) — native USB D− |
| 20 | Servo bus (via 1 kΩ) — native USB D+ |
| 21 | Display RST |
| 41 | TB6612 PWMB |
| 42 | TB6612 AIN2 |
| 43, 44 | **Leave free** — the board's COM port (USB‑serial chip): flashing + console |
| 45 | Display EN (backlight) |
| 47 | TB6612 BIN2 |
| 48 | Left encoder C2 |
| 38, 39, 40 | **Leave free** — used by built‑in SD slot on boards that have one |
| 26–37 | **Never use** — flash/PSRAM, board won't boot |

---

## Cautions
1. Set XY‑3606 to **5.0 V with nothing connected**, then wire the rest.
2. **All GNDs common.**
3. Keep motor/servo power wires away from mic/I2C wires.
