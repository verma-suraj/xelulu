# Eilik_Bot — ESP32 desk companion robot

Complete hardware + software build. The face is a custom renderer
(`EilikFace`) drawing the 38 expressions from `Animation sheet.png`,
animated per `Animations.md`: global breathing bob, organic randomized
blinks with squash-and-stretch, plus each face's scripted motion (falling
tears, beating hearts, steam puffs, flame flicker, counter-rotating
spirals, eye rolls, floating Zzz, drifting glints, blush pulses...).
In color: cyan base, pink blush/hearts/tongue, red/orange anger,
yellow stars, white glints.

```
Eilik_Bot/
├── Eilik_Bot.ino     personality engine: events -> face + sound + movement
├── Pins.h            ALL wiring + calibration knobs (single source of truth)
├── EilikFace.*       face renderer: 38 expressions + built-in animations
├── EilikSound.*      synth chirps + flash WAV clips, FreeRTOS task on core 0
├── EilikServos.*     LEDC servo driver + per-emotion movement sequencer
│                     (one movement per face, Eilik_Emotion_Movement_Datasheet.md)
├── EilikMotion.*     MPU6050 raw-I2C shake/knock/tilt detection
└── EilikTouch.*      ESP32 capacitive touch pads (head + body)
```

## System architecture

```
                         ┌──────────────── ESP32 DevKit (WROOM-32) ────────────────┐
                         │                                                          │
  senses (inputs)        │   CORE 1 (Arduino loop)          CORE 0                  │   outputs
                         │  ┌────────────────────────┐    ┌──────────────┐          │
 CST816S screen touch ───┼─►│  personality engine     │    │  sound task  │──I2S────┼─► MAX98357A ─► 4Ω 3W speaker
 head pad (GPIO32) ──────┼─►│  (event -> reaction     │───►│  chirp synth │          │
 body pad (GPIO14) ──────┼─►│   table + idle/sleep    │    └──────────────┘          │
 MPU6050 (I2C) ──────────┼─►│   state machine)        │───► LEDC 50Hz PWM ──────────┼─► 3x MG90S servos
                         │  │           │             │                              │
                         │  │           ▼             │                              │
                         │  │  face.Update() render   │───► SPI 40MHz ───────────────┼─► GC9A01 240x240 round TFT
                         │  └────────────────────────┘                              │
                         └──────────────────────────────────────────────────────────┘
```

- **Core 1** runs the render loop (~60 fps sprite push) plus all sensing and the
  behavior state machine — everything non-blocking, no `delay()` in the loop.
- **Core 0** runs the audio task; sounds are synthesized frequency sweeps
  (Eilik-style wordless chirps/trills) plus WAV clips baked into flash as
  const arrays — **no SD card needed**.
- Servos are hardware PWM (LEDC) — zero CPU cost; every emotion has its own
  timed step table from `Eilik_Emotion_Movement_Datasheet.md`.

## Wiring

### Display — SmartElex 1.28" round Touch LCD (GC9A01 + CST816S, 3V3 logic)
Silkscreen names as printed on the module:
| Silkscreen | ESP32 GPIO | Notes |
|---|---|---|
| VCC | 3V3 | |
| GND | GND | |
| LCD_D/C | **GPIO13** | (NOT GPIO2: display pull-up on the boot-strap pin blocked uploads) |
| LCD_CS | **GPIO5** | |
| SCLK | **GPIO18** | VSPI SCLK |
| MOSI | **GPIO23** | VSPI MOSI |
| MISO | — | leave unconnected (display never read) |
| TP_SDA | **GPIO21** | CST816S I2C |
| TP_SCL | **GPIO22** | CST816S I2C |
| TP_INT | **GPIO27** | |
| LCD_BL | 3V3 | backlight always on |
| TP_RST | **GPIO33** | |
| LCD_RST | **GPIO4** | |

### MPU6050 (I2C addr 0x68 — shares the display bus, per mpu6050_orientation.md)
| Module pin | ESP32 GPIO / rail |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | **GPIO21** (same wire as TP_SDA) |
| SCL | **GPIO22** (same wire as TP_SCL) |
| AD0 | GND |
| INT | not connected (polled) |

Mounting orientation differs from the sensor's silkscreen — `EilikMotion`
maps sensor axes to the robot frame (X fwd, Y left, Z up) exactly per
`mpu6050_orientation.md`; everything downstream uses robot-frame values only.
(`EilikMotion` auto-detects the bus: MPU pins == CST816S pins → reuses `Wire`,
anything else → its own `Wire1` bus.)

### Audio — MAX98357A I2S amp
| Module pin | ESP32 GPIO / rail |
|---|---|
| VIN | 5V (buck rail) |
| GND | GND |
| BCLK | **GPIO26** |
| LRC  | **GPIO25** |
| DIN  | **GPIO19** |
| GAIN | leave floating (9 dB) |
| SD   | leave as breakout default |
| + / − | speaker 4Ω 3W |

### Servos — 3x MG90S 360° continuous (signal is 3V3-tolerant-fine)
| Servo | Signal GPIO | Power |
|---|---|---|
| Left arm  | **GPIO16** | 5V buck rail |
| Right arm | **GPIO17** | 5V buck rail |
| Torso     | **GPIO15** | 5V buck rail |

The head servo is **not installed** (the head sits loose) — GPIO14, its old
signal pin, stays empty. GPIO15 can emit a brief pulse at boot — at worst a
tiny twitch, the 50 Hz servo signal takes over immediately.

### ESP32 touch pads (bare wire / copper foil under the shell)
| Pad | GPIO (touch channel) |
|---|---|
| Head pat | **GPIO32** (T9) |
| Belly/back | **GPIO14** (T6) |

Keep pad wires short (<10 cm) and away from servo wires. Detection uses
absolute bench-measured thresholds (`TOUCH_HEAD_THRESH` / `TOUCH_BODY_THRESH`
in `Pins.h`) — no boot calibration, re-measure with the tester sketches
(`touch_test/`, `Touch_final/`) if you change the shell or wiring.

### Power
```
LiPo ─► charger/protection board ─► buck converter set to 5.0–5.2 V ─┬─► ESP32 VIN (5V pin)
                                                                     ├─► 3x servo V+   ← 470–1000 µF cap across this rail
                                                                     └─► MAX98357A VIN
Display, CST816S, MPU6050 ─► ESP32 3V3 pin
ALL grounds common.
```
- 3x MG90S can spike >2 A at stall — size the buck ≥3 A and keep the big cap
  close to the servos, or the ESP32 will brown-out mid-gesture.
- Never power servos from the ESP32's 5V/3V3 pins.

## ⚠ The one hardware caveat: continuous-rotation servos

Your MG90S are **360° continuous** — they control *speed/direction*, not
*position*. Gestures are therefore timed out-and-back moves; small drift
accumulates and there is no "return exactly to center". It works and looks
lively, but for true held Eilik poses swap in standard 180° MG90S later —
only `EilikServos.cpp` would change.

Calibrate before first full run (knobs in `Pins.h`):
1. Power up with arms/torso detached from linkages.
2. Any servo creeping at rest → adjust its `SERVO_STOP_TRIM_*` until still.
3. Attach linkages, then tune gesture step `ms` values in `EilikServos.cpp`
   to your linkage throw. Add mechanical end-stops so drift can't wind past limits.

## Software setup

1. **Board**: ESP32 Dev Module, Arduino-ESP32 core 2.0.5+ or 3.x (both APIs handled).
2. **Libraries** (Library Manager): `TFT_eSPI`, `CST816S` — the two you already use. Nothing else.
3. **TFT_eSPI `User_Setup.h`** must contain:
   ```c
   #define GC9A01_DRIVER
   #define TFT_WIDTH  240
   #define TFT_HEIGHT 240
   #define TFT_MOSI 23
   #define TFT_SCLK 18
   #define TFT_CS    5
   #define TFT_DC   13
   #define TFT_RST   4
   #define SPI_FREQUENCY 40000000
   ```
   (If your display already works with different DC/RST pins, either rewire to
   the table above or update `Pins.h` + `User_Setup.h` together.)
4. Open `Eilik_Bot.ino`, upload. Serial monitor at 115200 shows boot status
   and every emotion as `emotion N/38 NAME`.
5. First flash: set `#define SELFTEST 1` in the .ino — it cycles every face,
   every sound, and pulses each servo so you can verify all wiring in one boot.
   Then set it back to 0.

## Personality map (what it does)

| Trigger | Reaction |
|---|---|
| Pat head pad | giddy / charmed blush / loving hearts (random), soft coo, wiggle |
| Pat head 4×+ in 10 s | embarrassed — eyes dart away, blush spreads |
| Touch body pad | cheeky tongue wiggle, giggle, arm wave |
| Poke screen 1–2× | playful wink, giggle |
| Poke screen 3–4× | annoyed — lid twitch + slow eye roll, grumble, flinch |
| Poke screen 5× | grumpy — brows dip, eyes dart |
| Poke screen 6× | fuming — red slits + steam puffs, torso tremor |
| Poke screen 7×+ | furious — trembling red eyes + flame flicker, arm-shake tremor |
| Knock the desk | wide-eyed wonder, rising coo, amazed rise |
| Shake it | confused spirals (sine-wave spin), wobble sound, torso wobble |
| Shake again < 12 s | furious |
| Third shake | crying softly — stretching falling tears, sad coo, arms droop |
| Pick up / tilt | scared/shivering — jitter + corner darts, until put down level |
| Put back down | giddy relief |
| Idle (random) | questioning, starry-eyed dance, playful wave, thoughtful, mischievous slide, bored, lonely pout, investigating, derp, sparkling joy, hungry fish, anticipation |
| Idle 45 s | big trembling yawn, then tired drooping lids |
| Idle 90 s | sleeping — breathing lines + floating Zzz, snores until touched |

Global animation rules (Animations.md) run on every face: 1–2 px breathing
bob, randomized 3–7 s organic blinks with squash-and-stretch and a tiny
open-bounce. Faces with scripted custom blinks (eager, disappointed,
flushed, bored...) override the global blink.

Sound character: all soft sine sweeps with 25 ms raised-cosine edges — no
square waves. Master volume: `SND_GAIN` in EilikSound.cpp (55 default,
30 = whisper, 120 = loud).

Skipped (add if you ever need them): battery voltage monitor (wire divider to
a free ADC pin), Wi-Fi/app control, MPU interrupt pin (polling at 30 ms is plenty).
