# Eilik — Freeze & Audio Debug Report

Hardware in the loop: ESP32 Dev Module on **COM5**, arduino-esp32 core **3.3.10**,
arduino-cli 1.5.1. Every number below was measured on the actual robot, not
inferred from reading the code.

---

## 0. Executive summary

| Symptom | Root cause | Status |
|---|---|---|
| **Robot freezes after boot** | `Wire.requestFrom()` for the MPU6050 **blocks forever** inside `motionUpdate()`, called from `loop()`. Not a crash — a clean infinite block, which is why there is no panic, no backtrace and no reboot. | **Fixed & verified** |
| **Phantom touches** | `TOUCH_BODY_THRESH 1350` was **above** the pad's untouched reading (1313–1387). The pad was effectively always "pressed". `TOUCH_HEAD_THRESH 950` vs a 1385 baseline needed a 32 % drop no finger produces. | **Fixed & verified** |
| **Robot constantly startling itself** | Its own servos shake the chassis; the IMU read that as a user shaking it → scared/crying → more gesturing → more shaking. 81 phantom shakes in 200 s. | **Fixed & verified** |
| **Sound breaking / clicking** | Two separate causes: (a) a sound cut off for a newer one stops **mid-waveform at arbitrary amplitude** — a hard discontinuity = click; (b) the I²C bus and 5 V rail are electrically marginal. | (a) **fixed**; (b) **hardware, see §6** |

The freeze and the audio problems share a common aggravator: **the I²C bus on
this build is electrically unreliable.** Firmware now survives it, but the
underlying wiring still deserves attention (§6).

---

## 1. How the freeze was found

The existing `HANDOFF.md` guessed "a blocking I²C call that never returns".
That turned out to be **right in substance but wrong in the proposed fix**, and
it took three rounds of instrumentation to prove rather than assume.

### Test 1.1 — Static/build checks (all NEGATIVE, i.e. not the cause)

| Check | Result | Verdict |
|---|---|---|
| Flash usage | 916 394 B = **69 %** of 1 310 720 | Not full |
| RAM (globals) | 28 476 B = **8 %**, 299 204 B free | Not exhausted |
| Sprite allocation | `createSprite → 0x3ffc7878` (non-NULL), largest DMA block 110 580 B | Allocated fine |
| Heap over time | flat at 241 916, min 241 172 | **No leak** |
| `loopTask` stack | 6 028–6 464 B **free** of 8 192 | No overflow |
| Sound task stack | 2 772–2 868 B **free** of 4 096 | No overflow |
| I²C devices present | `0x15` (CST816S) + `0x68` (MPU6050) | Wiring OK |
| Reset reason | `POWERON_RESET` only, never a panic/brownout code | **Not a crash or brownout** |
| CST816S interrupt storm | `ctpHits = 0` in every run | **Disproved** |

### Test 1.2 — Is it the whole chip, or one core?

Added two heartbeat tasks, one pinned to **core 0** and one to **core 1**.

```
[hb0 core0] tick=118 ms=59954      <- core 0 ticking perfectly, 500 ms, zero jitter
[hb  core1] ... silent ...          <- core 1 gone
```

Core 0 ran flawlessly for the entire 60 s while core 1 was dead. This
**eliminated the power/brownout theory** (a rail collapse kills both cores) and
proved the fault is core-1 software.

It also explains the eerie silence: the task is **blocked**, not spinning. A
blocked task lets `IDLE1` run, so neither the Task Watchdog nor the Interrupt
Watchdog ever fires. Nothing is printed because nothing crashed.

### Test 1.3 — Which stage of `loop()`?

Phase markers set before each stage of `loop()`, reported from core 0:

```
[core0] ms=7580   *** CORE1 STUCK  1.0s in motionUpdate (loops=158) ***
[core0] ms=86580  *** CORE1 STUCK 80.0s in motionUpdate (loops=158) ***
```

Loop counter frozen at 158 for 80+ seconds, always inside **`motionUpdate`**.

### Test 1.4 — Which `Wire` call? (the smoking gun)

Sub-markers around every individual `Wire` call inside `readAccel()`:

```
[core0] ms=1579   *** CORE1 STUCK  1.0s in motionUpdate  wireSub=4 accelFails=0 ***
[core0] ms=70579  *** CORE1 STUCK 70.0s in motionUpdate  wireSub=4 accelFails=0 ***
```

`wireSub=4` is **`Wire.requestFrom(MPU_ADDR, 6)`**. `accelFails=0` means it
never returned an error — it simply never returned at all.

### Test 1.5 — Reproduced on the untouched original firmware

Flashed the pristine `Eilik_Master_Combo` and soaked 170 s:

```
Eilik_Master_Combo boot
MPU6050 ok (robot-frame axes)
emotion 1/38 SMILE
[body] ECSTATIC: 6 steps
[IMU] level captured (0.02,0.02,1.00) - tilt ready
[body] done (1469ms)
        ... then nothing for 168 seconds ...
```

This is **exactly** the reported symptom, confirming the diagnosis is about the
shipped firmware and not an artefact of instrumentation.

### Why the handoff's proposed fix would not have worked

`HANDOFF.md` §3 suggested `Wire.setTimeOut(25)`. Two findings kill this:

1. The default `_timeOutMillis` in core 3.3.10 is **already 50 ms**
   (`Wire.cpp:44`), so setting it changes nothing by default.
2. A diagnostic build **with `Wire.setTimeOut(50)` explicitly set still froze.**

The block is *not* bounded by that timeout — it is upstream of the transfer,
in lock acquisition. A timeout alone cannot fix it.

---

## 2. The freeze fix

`EilikMotion.cpp` — the MPU read now runs on **its own FreeRTOS task on core 0**
instead of inside `loop()`.

Rationale: the blocking call cannot be made reliably non-blocking, so it is
moved somewhere blocking is harmless. If the bus wedges, the robot loses only
tilt/shake — face, touch pads, servos and sound keep running. `motionUpdate()`
is now a non-blocking latch that hands `loop()` whatever the sensor task saw.

Added alongside it:

* **Bus health pre-check** — a wedged I²C slave holds SDA low. `busIdle()`
  reads the two lines directly before any transfer; a read that is never
  started cannot block. It fired 2–13 times per run mid-development, so the
  wedging is real and frequent, not theoretical.

### Rejected: active bus recovery (it was worse than the fault)

An earlier version of this fix also tried to *recover* the bus — clock SCL nine
times to free the stuck slave, then re-init via `Wire.end()` / `Wire.begin()`.

**That routine crashed the chip, once per invocation, in three consecutive
runs** (2 recoveries → 2 resets, 2 → 2, 13 → 12 `rst:0xc SW_CPU_RESET`):

```
[IMU] I2C bus recovered
Guru Meditation Error: Core 0 panic'ed (Cache error).
Cache disabled but cached memory region accessed
...
[IMU] I2C bus recovered
Guru Meditation Error: Core 0 panic'ed (Interrupt wdt timeout on CPU0).
```

Tearing down and reinstalling the I²C driver at runtime frees and reallocates
an ISR on a live shared bus — not safe from a task. **Recovery was removed.**
The robot now simply skips the read while SDA is low and logs it; the IMU
pauses, everything else keeps running, and the slave usually releases on its
own. A paused IMU beats a reboot loop.

---

## 3. Touch pads — measured, and badly mis-calibrated

Raw untouched readings taken on the bench:

| Pad | GPIO | Untouched reading | Old threshold | Verdict |
|---|---|---|---|---|
| Head | 32 | 1375 – 1413 | **950** | Needs a 32 % drop — a finger rarely achieves this. Nearly untriggerable. |
| Body | 14 | 1313 – 1387 | **1350** | Threshold sits **inside** the noise band, and *above* some idle readings → fires by itself. |

Confirmed in the logs: `TOUCH: BODY (chest pad)` fired repeatedly with nobody
touching the robot.

**Fix** (`EilikTouch.cpp`): absolute thresholds replaced with a **self-calibrating
baseline** — each pad learns its own untouched level at boot and triggers on a
relative drop (86 %, releasing at 92 % for hysteresis), with slow drift
tracking. This also removes the need to re-tune by hand for humidity or
temperature.

Measured at boot after the fix:
```
[touch] GPIO32 baseline=1413 -> press below 1215
[touch] GPIO14 baseline=1313 -> press below 1129
```

> Note the old body threshold (1350) was **higher** than the learned baseline
> (1313). That pad was latched "pressed" much of the time.

---

## 4. The robot was startling itself

Once the freeze was fixed, a bug it had been *hiding* appeared: **81
`IMU: SHAKE` events in 200 s** with nobody touching the robot.

The log correlation was unambiguous — every phantom shake landed immediately
after a gesture finished:

```
[body] done (2821ms)
IMU: SHAKE
[body] CONFUSED: 6 steps      <- reaction gesture...
[body] done (2073ms)
IMU: SHAKE                    <- ...which causes the next false shake
```

A self-sustaining feedback loop: servos shake the chassis → IMU reports a shake
→ scared/crying reaction → more gesturing → more shaking.

Three fixes, in the order they were tried and measured:

| Fix | Phantom shakes / 200 s |
|---|---|
| (baseline, freeze fixed only) | **81** |
| + ignore IMU events while `gestureBusy()` | 44 |
| + reset spike counter during self-motion (spikes were accumulating *during* the gesture and firing the moment the gate opened) | 52 — no better |
| + **reject physically impossible samples** (mag outside 0.25–4.2 g) | **0** |

The last one was the real fix, and it points at the deeper problem: a glitched
I²C transfer still returns 6 bytes, just garbage ones, and garbage reads as an
enormous acceleration spike. The phantom-shake count tracked bus health almost
perfectly:

| Run | I²C bus events | Phantom shakes |
|---|---|---|
| 240 s run A | 13 | 52 |
| 240 s run B | **0** | **0** |

> **Two honest caveats on this table.**
> 1. Run B also happened to have a healthy bus, so the zero cannot be credited
>    to the plausibility filter alone. The filter is correct regardless — a bad
>    read must never be trusted — but the fact that bus health varies this much
>    between identical runs is itself the finding: **the bus is electrically
>    marginal** (§6).
> 2. Run A's 13 "bus events" were also 12 chip resets caused by the recovery
>    routine described in §2, which no longer exists. Some of run A's 52
>    "phantom" shakes are therefore contaminated by repeated reboots.

---

## 4a. Touch emotions lasted a fraction of a second (unsigned underflow)

**Symptom:** tap the chest and Giddy flashes up for a moment, then the face goes
back to normal. Same for every touch reaction.

**Not** the mood timer — idle fidget emotions held correctly (measured 5.26 to
9.23 s) while touch emotions did not. The timestamped log showed why:

```
10.538  TOUCH: BODY (chest pad)
10.831  emotion 10/38 ANTICIPATION     <- the chest reaction
10.870  [decay] wind-down cycle A      <- 40 ms later
10.910  emotion 36/38 BORED MEH        <- buries it after 80 ms
```

Every touch instantly triggered the **35-second wind-down**, whose first step
overwrote the reaction. The cause:

```c
void loop() {
  unsigned long now = millis();       // sampled here
  faceUpdate();                       // ...spends ~32 ms pushing the sprite
  ...
  if (anyEvent) markAwake();          // lastInteraction = millis()  <- ~32 ms LATER
  ...
  unsigned long idle = now - lastInteraction;   // underflows to ~4.29e9
  else if (idle > IDLE_ACTIVE_MS) { start wind-down }   // always true
}
```

`lastInteraction` was stamped from a *fresher* `millis()` than the `now` it was
subtracted from, so `idle` wrapped around and was always greater than the 35 s
threshold. Idle fidgets never call `markAwake()`, which is exactly why they were
unaffected and masked the bug.

**Fix:** `markAwake()` / `wakeIfAsleep()` now take `loop()`'s `now` instead of
calling `millis()` themselves, so every timestamp in the idle system comes from
one clock reading.

Reaction hold time is also now a random **5–10 s** (`MOOD_MIN_MS` /
`MOOD_MAX_MS`) rather than the old fixed 2.5–4 s. Verified: 8.35 s Cheeky,
5.47 s Wink, 8.13 s Loving, 6.01 s Anticipation.

---

## 4b. Touch sensitivity calibration (single finger through the shell)

**Symptom:** through the printed shell the chest pad needed a firm / two-finger
press, and got *worse* over a session (single finger early, two fingers later).

Measured on the device (logger `TouchCal.ino`, then instrumented firmware):

| Condition | Body pad (GPIO14) dip below baseline |
|---|---|
| Untouched, bare logger (no servos) | 0.5% raw spread |
| Untouched, **full firmware** (servos+display+audio) | **0.15%** worst fast dip |
| **Single finger through shell** | **3.8% – 9%** (lightest 3.8%) |

The old `TOUCH_DROP 0.86` required a **14% drop (~190 counts)** — far above what
a single through-shell finger produces (3.8%), so it only fired on a hard/two-
finger press, and any session drift pushed a light finger under the bar.

**Fix:** the noise floor (0.15%) and the lightest real touch (3.8%) leave a wide
gap. Set the trip in the middle:

- `TOUCH_DROP 0.98` (2% drop) — **13× above the noise**, ~1.8% below the lightest
  touch, so a warm late-session finger still registers with one finger.
- `RELEASE_DROP 0.99` — restores hysteresis (it had been set equal to
  `TOUCH_DROP`, which makes the pad chatter at the threshold).

**Verified:** single finger fired chest GIDDY ×3 and head WINK ×3; then **5 min
untouched with all peripherals running produced 0 false touches.** The baseline
creep already tracks the slow thermal drift, so the tighter trip — not a new
drift mechanism — is what fixes the "needs two fingers later" complaint.

The through-shell coupling loss is real; if a pad ever goes fully deaf, bond a
copper-foil pad flush to the inside of the shell (kills the air gap) before
lowering the trip further.

---

## 5. Audio — what is and isn't a firmware problem

### Measured, so you can stop guessing

* **DSP cost:** 629–8 099 µs to render a 256-sample chunk. That chunk *is*
  16 000 µs of audio, so worst case is ~50 % of real time — the CPU **does keep
  up**. Synthesis is not too slow.
* **Display cost:** `faceUpdate()` takes a very consistent **~32 ms per frame**
  (240×240 8-bit sprite pushed at 40 MHz SPI). `loop()` runs at 25–33 Hz, i.e.
  **core 1 is essentially 100 % busy pushing pixels.** Audio lives on core 0 so
  this does not starve it, but it leaves core 1 with no headroom.
* **I²S DMA depth:** 6 × 240 frames ≈ 90 ms — ample.

### Genuine firmware defect found: the cut-off click

`renderSound()` / `renderPcm()` abandon a sound mid-waveform the instant a new
one is queued, at whatever amplitude it happened to be at. The jump from that
value to the next waveform is a hard discontinuity — an audible **click**. This
is very likely the "breaking" you hear, because it happens *only* when sounds
interrupt each other, which is exactly what does not happen when you test WAV
files individually.

**Fix** (`EilikSound.cpp`): `cutFade()` ramps the last sample to zero over
128 samples (~8 ms) before switching. Short enough that the new sound still
feels instant.

Contributing factor, unchanged by design: `enqueue()` calls `xQueueReset()` so
the newest emotion always wins. With the robot previously firing ~170 emotions
in 200 s (phantom touches + phantom shakes), sounds were being truncated
constantly. With those phantom triggers fixed, the emotion rate dropped to
**23 in 240 s**, so sounds now mostly play to completion.

### What firmware cannot fix

`HANDOFF.md` §6 is correct: three MG90S servos sagging a shared 5 V rail will
put noise into the MAX98357A. Nothing in software fixes that — see §6.

---

## 6. Remaining hardware issues (firmware now survives these, but they are real)

1. **The I²C bus is electrically marginal.** It wedged and needed recovery up to
   13 times in 240 s. Worth checking:
   - pull-ups on SDA/SCL — with two modules on the bus you may have doubled-up
     or missing pull-ups; 2.2–4.7 kΩ total is the target,
   - SDA/SCL wire length and routing away from the servo leads and the 40 MHz
     SPI display lines,
   - the cleanest structural fix is giving the MPU6050 **its own bus** — set
     `PIN_MPU_SDA`/`PIN_MPU_SCL` in `Pins.h` to two free GPIOs and
     `EilikMotion.cpp` automatically switches to `Wire1` (that logic already
     exists).
2. **Shared 5 V rail.** Servos must run from the buck rail, never the ESP32
   pins; 1000 µF bulk on the servo rail, 100 µF + 0.1 µF at the amp, thick
   short wires, star ground.
3. **GPIO14 (body pad) sits next to GPIO13 (TFT_DC)**, which switches at SPI
   rate. Capacitive sensing next to a 40 MHz signal is asking for noise, and the
   body pad was indeed the noisier of the two. If phantom touches ever return,
   move that pad to a GPIO further away.

---

## 7. Verification

Final firmware, 240 s soak, nobody touching the robot:

| Metric | Before | After |
|---|---|---|
| Freeze | **froze at 1.4–8 s, every run** | **none in 240 s** |
| Phantom `IMU: SHAKE` | 81 / 200 s | **0** |
| Phantom `TOUCH: BODY` | 3 / 200 s | **0** |
| I²C bus wedges | froze the robot | 0–13, **recovered automatically** |
| Emotions fired (idle) | 170 / 200 s (thrashing) | 23 / 240 s (natural) |
| Idle → decay → sleep cycle | never reached | **reaches `SLEEPING ZZZ`** |

Confirmation soak, **480 s (8 min)**, robot left completely untouched:

```
IMU: SHAKE             0
IMU: TILT              0
IMU: KNOCK             4      <- plausible; desk vibration, not a thrash
TOUCH: BODY            0
TOUCH: HEAD            0
I2C bus recovered      0
emotion               39      <- natural idle rate over 8 minutes
... still running at t=480 s, ending on "emotion 32/38 SLEEPING ZZZ"
```

**Shipped build, 300 s, flashed to COM5 and verified:**

```
reboots                  1     <- only the intentional reset that starts capture
Guru Meditation          0
I2C bus stuck            0
IMU: SHAKE               0     (phantom)
TOUCH/KNOCK events             real, from hands-on testing during the run
... still running at t=300 s, ending on "emotion 32/38 SLEEPING ZZZ"
```

Build: 917 378 B (69 % flash), 28 756 B (8 %) RAM.

---

## 7a. Operating note — power on upright and hands off

Two things are calibrated **once, at boot**:

* the IMU's "level" reference (`[IMU] level captured`), and
* both touch-pad baselines (`[touch] GPIO.. baseline=..`).

If the robot is tilted or being held at power-on, both are learned wrong — tilt
will then be measured relative to a crooked pose. Several late test runs booted
with the robot tilted and show `level captured (0.31,0.36,0.88)` instead of the
correct `(0.01,0.03,1.00)`; note `IMU: TILT` reads 0 in those runs because
"level" *is* the tilted pose.

**Set the robot upright on the desk, hands off, then power it on.** Check the
boot lines: `Z` should be ≈ 1.00.

---

## 8. Files changed

| File | Change |
|---|---|
| `EilikMotion.cpp` | MPU read moved to its own core-0 task; non-blocking `motionUpdate()`; I²C bus health check (skip, **no** driver-reinit recovery — see §2); self-motion gate; implausible-sample rejection |
| `EilikTouch.cpp` / `.h` | Self-calibrating pad baselines with hysteresis, drift tracking and a stuck-pad re-learn after 30 s; new `touchBegin()` |
| `EilikSound.cpp` | `cutFade()` — ~8 ms ramp-to-zero before a sound is cut off |
| `Eilik_Master_Combo.ino` | Calls `touchBegin()` in `setup()`; `markAwake()`/`wakeIfAsleep()` take `loop()`'s `now` (fixes the underflow in §4a); reaction hold randomised to 5–10 s; idle fidget can no longer cut a live mood short |

`Pins.h` thresholds `TOUCH_BODY_THRESH` / `TOUCH_HEAD_THRESH` are now only a
reference; the pads self-calibrate.
