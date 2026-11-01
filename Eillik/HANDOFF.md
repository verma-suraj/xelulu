# Eilik Robot — Project Handoff / Continuation Notes

> Read this first if you're picking up this project in a new chat/account.
> It captures the hardware, the repo layout, what was done, the calibration
> knobs, and the **one open bug** (robot freezes after boot).

Repo: https://github.com/verma-suraj/eillik  •  Main branch is up to date (commit `b64c193`).

---

## 1. What this project is
An Eilik-style desk robot on an **ESP32 Dev Module (WROOM-32)**:
- **Face:** 1.28" round GC9A01 240×240 display, 38 animated expressions.
- **Voice:** MAX98357A I²S amp + speaker; 38 synth effects + a few WAV clips (audio runs on a FreeRTOS task on core 0, non-blocking).
- **Body:** 3× **MG90S 360° CONTINUOUS-ROTATION** servos — left arm, right arm, torso. **No head servo** (head sits loose).
- **Senses:** CST816S capacitive touchscreen (I²C), 2× ESP32 capacitive touch pads (head + body/chest), MPU6050 IMU (I²C).

### ⚠️ Critical hardware truth (caused a lot of confusion — don't re-learn it the hard way)
The arm/torso servos are **360° continuous-rotation** (speed + direction only, **no angle feedback, some drift is unavoidable**). They are **NOT** positional 180° servos. The movement code is therefore a **speed/time model** with net-zero (out-and-back) gestures + drift calibration (`STOP_TRIM`, `rev_gain`, `rev_down`). A "positional/angle-based" `Body_Movement_Test.ino` briefly existed and was **WRONG** — it has been dropped from `main`. The correct `Body_Movement_Test.ino` on `main` says "OPEN-LOOP … 360-degree continuous-rotation" at the top.

---

## 2. Which sketch to flash
**Flash `Eilik_Master_Combo/Eilik_Master_Combo.ino`** — the full robot (face + voice + body + touch combos + MPU).

Other folders:
| Folder | Purpose |
|---|---|
| `Eilik_Master_Combo/` | **Main firmware** (full combo touch triggers). Flash this. |
| `Eilik_Master_Core/` | Simpler master (basic touch). Alternative. |
| `Eilik_Bot/` | Earlier bench/test sketch. Has a handy **serial console (type 1–38 to play any emotion)** + `TESTING_DATASHEET.md`. Good for testing; not the final robot. |
| `Body_Movement_Test/` | Continuous-rotation arm/torso calibration (STOP_TRIM, rev_gain). |
| `MPU6050_Expression_Test/` | IMU expression test. |

### Build settings
- Board: **ESP32 Dev Module**
- Serial Monitor: **115200 baud**
- Libraries: **TFT_eSPI** (its `User_Setup.h` MUST match `Pins.h`) and **CST816S**
- Opening the `.ino` loads the whole folder's `.cpp/.h` — you flash the sketch, not individual files.

---

## 3. 🐞 OPEN BUG (unresolved — start here)
**Symptom:** With `Eilik_Master_Combo`, the robot **freezes right after boot**. Neither touch nor tilt produces any reaction. Serial prints `[IMU] level captured …` **once** and then nothing.

**Note:** `[IMU] level captured` printing **once is correct/by design** (tilt captures the "level" gravity direction a single time at boot). That line is NOT the bug.

**Diagnosis so far:** Touch pads are capacitive GPIO; tilt is I²C — separate systems. For BOTH to die together, the whole `loop()` is frozen, most likely on a **blocking I²C call that never returns**. The **MPU6050 and the CST816S touchscreen share the same I²C bus (GPIO 21/22)** — the classic contention/lockup point (also flagged in `.kiro/specs/eilik-bot-logic-fix/` bug 1.9).

**Pending info to collect (ask the user / observe):**
1. Is the on-screen face **frozen** or still gently blinking/breathing? (frozen ⇒ loop hung)
2. **Unplug only the MPU6050 SDA+SCL**, power on. If touch + face come alive ⇒ shared-I²C-bus lockup confirmed.
3. Serial output **after** the `[IMU] level captured` line: crash backtrace (`Guru Meditation`) ⇒ crash; silence ⇒ hang; `MPU6050 not found` ⇒ wiring.

**Most likely fix (not yet applied):**
- Add an I²C timeout so a stuck read can't hang forever: `Wire.setTimeOut(25);` in `setup()` after `screenTouch.begin()`; and/or
- Give the MPU its **own bus** (`Wire1` on different pins) — `EilikMotion.cpp` already auto-uses `Wire1` if `PIN_MPU_SDA/SCL` in `Pins.h` differ from the CST816S pins. This needs rewiring the MPU to 2 free GPIOs.
- Also verify boot line says `MPU6050 ok` (not "not found").

---

## 4. What was changed this session (in `Eilik_Master_Combo`)
All committed to `main` (`b64c193`):
1. **Orientation-agnostic tilt** (`EilikMotion.cpp`): the MPU is mounted in a "random" direction, so instead of assuming which axis is up, it **captures the level gravity vector once at boot** and measures the **angle** away from it. Works for any mounting. **Set the robot upright at power-on** so "level" is captured correctly.
2. **No yawn/sleep on touch** (`.ino`): removed the wake chain. Touch/motion now wakes straight to a neutral face + the interaction's own reaction. Yawning/Sleepy/Sleeping appear **only inside a decay cycle**, never from a touch.
3. **Engaging idle/decay** (`.ino`): livelier random "fidget" every **7–16 s** from an 18-face fun pool, then **one of 3 randomly-chosen wind-down cycles** into sleep (no more monotony). Tunables at top of the `.ino`: `IDLE_ACTIVE_MS`, `FIDGET_MIN/MAX_MS`, `DECAY_A/B/C`.
4. **Serial logging**: every touch prints its location (`TOUCH: HEAD/BODY/FACE`) and every IMU event prints (`IMU: TILT/SHAKE/KNOCK/UPRIGHT`).

`Eilik_Bot` (separate sketch) also got: all 38 emotions reachable, a **serial test console (type 1–38 + Enter to play any emotion)**, wake recovery, non-repeating idle decay, per-step servo slew, and `TESTING_DATASHEET.md`. Consider porting the serial console into `Eilik_Master_Combo` — it's the best way to test every emotion/gesture on demand.

---

## 5. Calibration knobs (hardware never matches paper)
All in `Eilik_Master_Combo/Pins.h` unless noted:
- **Touch thresholds** (hardware/humidity dependent — re-measure!): `TOUCH_BODY_THRESH 1350`, `TOUCH_HEAD_THRESH 950`. `touchRead()` reads **LOWER** when touched. If touch doesn't register, print the raw values and set the threshold between the untouched and touched readings. **The head threshold (950) may be too strict** for a single finger — verify.
- **Servo stop trims** (continuous servos creep at "stop"): `SERVO_STOP_TRIM_ARM_L/ARM_R/TORSO`, and `SERVO_SPEED_RANGE_US 400`.
- **Servo direction / gravity trim** (in `EilikServos.cpp`): `SERVO_DIR[3]` (mounting direction), `REV_GAIN[3]` (trims the gravity-DOWN stroke), `REV_DOWN[3]` (which sign is physically "down"), `RAMP_MS 70` (per-step slew for smooth, non-jerky motion).
- **Motion thresholds:** `SHAKE_SPIKE_G 0.55`, `KNOCK_MIN_G 0.20`, `TILT_DEG 45`.

---

## 6. Known constraints / gotchas
- **Audio crackles while servos move** = the 3 servos sag the **shared 5V rail**. Firmware (per-step slew) only softens it; the real fix is **power**: give the MAX98357A a clean supply and/or add bulk capacitance (1000 µF on servo rail, 100 µF + 0.1 µF at the amp), thick short wires, star ground. Servos must be powered from the buck 5V rail, **not** the ESP32 pins.
- **Only 3 touch surfaces** (head pad, body pad, screen), so the datasheet's multi-sensor "combos" are approximated — several emotions are only reachable via idle fidgets / the serial console, not a dedicated touch.
- **Head servo is not installed** — no head motion anywhere.
- `PIN_TOUCH_BODY = GPIO14` (former head-servo pin), `PIN_TOUCH_HEAD = GPIO32`.
- GC9A01 `TFT_DC` is on GPIO13 (not GPIO2 — a strap pin that blocked uploads).

---

## 7. Pin map (from `Pins.h`)
| Function | GPIO |
|---|---|
| TFT SCLK / MOSI / CS / DC / RST | 18 / 23 / 5 / 13 / 4 |
| I²C SDA / SCL (CST816S **and** MPU6050) | 21 / 22 |
| CST816S RST / INT | 33 / 27 |
| I²S BCLK / LRC / DIN | 26 / 25 / 19 |
| Servo arm L / arm R / torso | 16 / 17 / 15 |
| Touch pad head / body | 32 / 14 |

---

## 8. Git state
- `main` = `origin/main` = commit `b64c193` (this work + correct files), pushed to GitHub.
- Feature branch `claude/master-combo-code-fixes-6bfdd1` on GitHub has the same work.
- End commit signature convention used: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.

## 9. Immediate next steps
1. **Fix the boot freeze (Section 3)** — collect the 3 data points, then add the I²C timeout / bus fix.
2. Confirm `MPU6050 ok` at boot; verify tilt fires after the robot is set upright at power-on.
3. Calibrate touch thresholds (Section 5) using raw serial readings.
4. Address the shared-5V-rail power issue (Section 6) to stop audio crackle.
5. Optional: port the `Eilik_Bot` serial console (1–38) into `Eilik_Master_Combo` for easy testing.
