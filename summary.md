# ZivikaLabs — Companion Robots: Project Summary

_Last updated: 2026-10-19_

Working notes + decisions for the companion-robot family. Source of truth for features is the **Product Feature Catalogue** (`xelulu .docx`); this file summarises what we decided, what was built, and what's still open.

---

## 1. The product family (from the catalogue)

One shared platform, four age-targeted companions:

| Product | Audience | Role | Form factor |
|---|---|---|---|
| **Xelulu** | kids 1–12 | screen-free AI playmate (plays, Grandma stories, learning, comfort) | egg-shell body, visor face, 2 wheels, little arms |
| **Sathi / WithU** | teens 12–20 | witty desk study-friend | desk unit |
| **Parth** | adults 20–45 (flagship) | AI chief-of-staff / mentor / webcam | static faceted orb w/ touch face |
| **Sarthi** | 45+/60+ | elder companion (calls, meds, safety, scam shield) | Parth + ears |

Work so far has focused on **Xelulu** (full hardware + wiring + software spec) and **Parth** (ESP32-S3 budget BOM). Sathi and Sarthi not started.

---

## 2. Shared architecture decision

**All AI runs in the cloud; the on-device MCU is a gateway.** The device streams mic audio up, plays TTS back, renders cloud-driven UI, and runs the real-time I/O (motors, touch, sensors, face). This keeps the hardware cheap (ESP32-class, no Linux SoC) and is identical across products — they differ by shell + peripherals, not by brain.

Interface between hardware and software = a frozen **device gateway API** (MQTT/WebSocket; command/telemetry/event/audio messages). Both teams build to it in parallel and integrate late via a device simulator. Full detail in `Xelulu_Software_Spec.md`.

---

## 3. Xelulu — FINAL hardware (purchased; see invoice INV2627/280043)

Screen-free kids' playmate. ESP32-S3 gateway + cloud AI.

| Subsystem | Part |
|---|---|
| Brain | ESP32‑S3‑DevKitC‑1 **N16R8** (16MB flash, 8MB PSRAM) |
| Drive | 2× N20 6V 200RPM encoder motors + **TB6612FNG**; 34 mm wheels + ball casters |
| Arms + head | **Waveshare SC09** 2.3 kg serial-bus servos (feedback), head pan + 2 arms, 1 DoF each |
| Face display | **SmartElex 2" IPS** (ST7789 SPI) behind the visor _(GC9A01 1.28" round bought as alternate)_ |
| Mics | 2× **GY‑SPH0645** I2S MEMS |
| Speaker | **MAX98357A** I2S amp + 28/30 mm 8Ω speaker |
| Touch | **Adafruit MPR121** (I2C, per-zone configurable sensitivity): head, back, belly, 2 hands |
| IMU | **GY‑87** (MPU6050 + mag + baro) — tilt / shake / pickup |
| Distance | 4× **VL53L0X V2** ToF via **TCA9548A** mux: 2 front + 2 cliff (no bumper) |
| Storage | DFRobot MicroSD (offline songs/stories cache) |
| Power | **2S**: 2× LG 18650 (series) + 2S BMS + Type‑C→2S charger (1794301) + XY‑3606 5V buck + AMS1117 3.3V |

**Key design calls along the way:**
- Dropped the original "service robot with arms" misread — Xelulu is a small kids' companion.
- Casters ×2 (stability); head = pan only; **all face expressions on the display** (no separate LEDs).
- Servos: serial-bus **SC09** chosen for best angle control + feedback; driven straight from the ESP UART (half-duplex via a 1 kΩ resistor / 74HC125) — **no separate servo-controller board needed**.
- No camera; dual mic + speaker only.
- Collision = 2 ToF; cliff = 2 ToF; bumper removed.

**Wiring:** full power chain, ESP32-S3 GPIO map, and per-module connection tables in **`Xelulu_Wiring.md`**. Highlights: never use GPIO 26–37 (flash/PSRAM); I2C on 8/9; separate I2S for mic (4/5/6) and speaker (15/16/7); SPI shared display+SD (12/11/13); servo bus on UART 43/44; set the buck to 5.0 V before loading; one common ground.

---

## 4. Parth — ESP32-S3 budget build (BOM done)

Static desk companion (chief-of-staff). Decided to use **ESP32‑S3 as a pure cloud gateway** (no local AI), **battery powered**, **no camera**, budget ~₹5k.

| Subsystem | Part |
|---|---|
| Gateway | ESP32‑S3 N16R8 (Robu/Robocraze OOS → source Graylogix; N8R8 is a fine fallback for a gateway) |
| Display | **SmartElex 3.5" HMI** resistive touch, 320×480, UART smart-screen (renders its own GUI; offloads the ESP) |
| Mic | INMP441 I2S (Robocraze) |
| Audio out | MAX98357 + speaker |
| Calls | external **BT-Classic audio module w/ mic** (ESP32-S3 is BLE-only → can't do HFP itself) |
| Clock | DS3231 RTC (Robocraze) |
| Power | 2× LG 18650 + 10W 5V charge/boost + Type-C port + holder + switch |

Final sheet: **`Parth_BOM_ESP32S3_v4.xlsx`** (~₹4.4k, every line verified in stock on its product page; parts split across Robu + Robocraze + Graylogix).

**What ESP32-S3 Parth cannot do (needs a Linux SBC):** on-device camera / video calls / webcam, camera-vision coaching (auto-framing, posture, eye-contact). Handle video in the phone app, or do an SBC build later. Also no Bluetooth-Classic without the external module; smart home = Matter-over-Wi-Fi (no Thread radio).

---

## 5. Files in this folder

| File | What it is |
|---|---|
| `xelulu .docx` | the product feature catalogue (source of truth) |
| `Xelulu.jpeg`, `parth.jpeg`, `Sarthi.jpeg` | design renders |
| `Xelulu.pdf` | purchase invoice (the actual Xelulu components) |
| `Xelulu_BOM.xlsx` | Xelulu hardware BOM (with buy links, specs, stock, notes) |
| `Xelulu_BOM_OOS_Replacements.xlsx` | in-stock replacements for parts that went OOS |
| `Xelulu_Software_Spec.md` / `.pdf` | software-team spec + device gateway API contract |
| `Xelulu_Wiring.md` | full system wiring guide (power, GPIO map, per-module) |
| `Parth_BOM_ESP32S3_v4.xlsx` | Parth ESP32-S3 budget BOM (final; v5 updates in-place: adds HMI micro-SD, 1S protection PCB, fuse, CR2032) |
| `Parth_Software_Spec.md` | Parth software-team spec — AI-feature-focused; reuses Xelulu gateway |
| `summary.md` | this file |

Two Claude Docs were also created online: the P0 technical proposal and a software-spec doc.

---

## 6. Open items / next steps

- **Xelulu — NFC:** decide whether to add a PN532 (needed for treasure-hunt P0 + code-card features) or defer.
- **Parth — BT module:** confirm it supports **HFP** (call answer/hang-up), not just A2DP music; else use a BK8000L.
- **Parth — N16R8 stock:** scarce on Robu/Robocraze; Graylogix/Amazon listed. Verify at order (N8R8 is an acceptable gateway fallback).
- **Parth — software spec:** DONE (`Parth_Software_Spec.md`) — reuses Xelulu gateway, documents only the delta (new events/commands, HMI GUI pages, connections model) and expands every PA-* AI feature into software deliverables + priorities.
- **Sathi / WithU and Sarthi:** BOMs + wiring not started (Sarthi = Parth platform + ears).
- Robu/Robocraze stock churns fast — **re-verify every cart at order time.**
