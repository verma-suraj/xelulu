# Eilik_Bot — Testing Datasheet (matches the current firmware)

Everything you can trigger on the robot and how to verify it on the Serial
Monitor (**115200 baud**). This sheet is generated from the actual code in
`Eilik_Bot.ino`, `EilikServos.cpp`, `EilikSound.cpp`, and `EilikTouch.cpp` —
not the design spec. Where the hardware only has 3 touch surfaces, the
datasheet's sensor combos are approximated (see notes).

Head servo is **not installed** — "torso" = the single 180° left-right base
servo; "arms" = the two arm servos. All angles are approximations (continuous
servos have no position feedback).

---

## 1. The fastest way to test everything — Serial console

Open the Serial Monitor, set line ending to **Newline** (or "Both NL & CR"),
then type:

| You type | What happens |
|---|---|
| `1`…`38` + Enter | Plays that emotion now — face **+** sound **+** body movement. Wakes the robot if asleep. |
| `h` (or `?`) | Prints the full 1–38 emotion list |
| `t` | Prints the **live raw values** of the head & body touch pads (for calibration) |
| `w` | Wake / reset to Smile |
| `s` | Force the robot to fall asleep (skips the idle wait) |

Because a number always wakes the robot and plays the exact emotion, this is
how you confirm all 38 gestures/sounds regardless of touch wiring.

---

## 2. Emotion table (index → movement → sound)

Sound column: every emotion has a synth effect. Emotions marked with a **clip**
play that WAV ~50% of the time instead (for variety).

| # | Emotion | Body movement (arms / torso) | Sound |
|---|---|---|---|
| 1 | SMILE | rest — servos stopped | synth chirp |
| 2 | GIDDY | arm bounce + ±15° torso wiggle | synth / **RISA** clip |
| 3 | ECSTATIC | arms shoot up + ±45° double sweep | synth arpeggio |
| 4 | CHARMED | slow "aww" to chest, ~20° turn, hold | two-tone chime |
| 5 | SPARKLING JOY | ta-da sweep + ±60° decelerating | sparkle arpeggio |
| 6 | PLAYFUL WINK | one-arm wave, ~15° turn + snap back | wink blip |
| 7 | STARRY EYES | one-arm wave, ±20° rock *(reuses Playful)* | synth / **SOARYN** clip |
| 8 | CHEERFUL LAUGH | alternating shimmy, ±10° @ ~3 Hz | synth / **RISA** clip |
| 9 | EAGER | lean-in arms, ~30° turn, hold | rising blips |
| 10 | ANTICIPATION | rise + pause mid-motion (held turn) | climbing ticks |
| 11 | POUT | arms cross low, ~20° away, hold | digital sigh |
| 12 | TEARY EYES | droop, one arm wipes eye, ~15° drift | wobbly descent |
| 13 | CRYING SOFTLY | cover face, ±5° tremor near center | sob tones |
| 14 | DISAPPOINTED | sudden arm drop, ~25° off, slow creep back | "womp" |
| 15 | HEARTBROKEN | limp droop, slow turn to limit, hold | bleep → buzz |
| 16 | GRUMPY | arms cross firm, torso locked center | buzzy grumble / **BAHHH** clip |
| 17 | FURIOUS | stiff raise + vibrate, ±8° tremor | buzz burst / **ARGHH** clip |
| 18 | FUMING | sharp overhead vent, ±15° faster tremor | tick → static |
| 19 | ANNOYED | hands-on-hips snap, ~15° away + snap back | flat monotone / **BAHHH** clip |
| 20 | FLUSHED | cover face, retreat halfway, small wobble | rising trill |
| 21 | QUESTIONING | one arm palm-up "what?", torso static | "hm?" / **SUS** clip |
| 22 | INVESTIGATING | pointing arm, slow full **±90° scan** | sonar pings / **SUS** clip |
| 23 | THOUGHTFUL | arm to chin, hold, torso static | pondering tones |
| 24 | CONFUSED SPIRAL | alternating arm circles, ±30° fast wobble | wobble / **WHAT** clip |
| 25 | WONDER | slow amazed rise, ~20° turn, hold | slide-up / **OI** clip |
| 26 | PLAYFUL | one-arm wave, ±20° bouncy rock (~2 Hz) | playful blips / **HEHE** clip |
| 27 | MISCHIEVOUS | scheming hand-rub, ~20° sneak turn | quiet→loud / **HEHE** clip |
| 28 | CHEEKY TONGUE | cheeky flick up, ~15° snap + back, drop | springy boing / **BAHHH** clip |
| 29 | SILLY DERP | floppy async flops, uneven stagger | derpy tones / **OI** clip |
| 30 | LOVING | self-hug sweep, ~15–20° turn, hold | "ba-bum" heartbeat |
| 31 | TIRED | slow droop, low-energy drift, hold | slowing hum |
| 32 | SLEEPING ZZZ | **servos de-energized (torque off)** | fading snore (loops) |
| 33 | YAWNING | slow stretch up+out, gentle side turn, hold | yawn slide / **PIGYAWN** clip |
| 34 | SLEEPY | heavy half-lift settling, drift + corrective snap | drowsy tones |
| 35 | SCARED | fast defensive flinch, snap away + decaying tremor | startle beeps |
| 36 | BORED MEH | loose hang, occasional single-arm shrug | unenthused "meh" |
| 37 | EMBARRASSED | one arm covers face, ~20–30° away + fast tremor | trill, cut |
| 38 | HUNGRY | two "gimme" reach cycles toward chest | pleading pings |

Verify a movement: the Serial Monitor prints
`[Gesture] Start: emotion=… steps=…` then one `[Gesture] Step k/n armL=… armR=…
torso=…` per step, then `[Gesture] Complete: elapsed=…ms`.

---

## 3. Touch triggers (3 surfaces)

Every touch prints where it landed, e.g. `TOUCH: HEAD (top pad) (raw=1041
thr=950)` or `TOUCH: FACE (screen)`.

| Surface | Pin | Repeat behaviour (counter resets after idle) | Emotions |
|---|---|---|---|
| **HEAD** pad | GPIO32 (T9) | random each pat; 4+ pats within 10 s → overwhelmed | GIDDY / CHARMED / LOVING → **EMBARRASSED** |
| **BODY / chest** pad | GPIO14 (T6) | 1st → 2nd–3rd → 4th+ | GIDDY → CHARMED → **LOVING** |
| **FACE** (screen) | CST816S (I2C) | poke escalation, resets after 8 s | WINK×2 → ANNOYED×2 → GRUMPY → FURIOUS → **FUMING** |

Touch thresholds live in `Pins.h` (`TOUCH_HEAD_THRESH`, `TOUCH_BODY_THRESH`).
touched reads **lower** than the threshold. If a pad isn't responding, type `t`
in the console, note the untouched vs. touched raw numbers, and set the
threshold between them.

---

## 4. Motion triggers (MPU6050)

Every motion event prints, e.g. `IMU: SHAKE`.

| Motion | Detection | Emotions (repeat resets after idle) |
|---|---|---|
| **Knock** — one desk tap | single spike >0.20 g + 300 ms quiet | WONDER |
| **Shake** — repeated | 3+ spikes >0.55 g within 800 ms | CONFUSED → FURIOUS → TEARY → CRYING → DISAPPOINTED → **HEARTBROKEN** |
| **Tilt** — picked up | held >45° for 400 ms | **SCARED** (holds until put down) |
| **Upright** — put back down | level again for 600 ms | GIDDY (relieved) — only after a tilt |

---

## 5. Idle life & sleep chain (no interaction)

While active, Eilik fidgets every 18–40 s with a random light emotion
(questioning, starry, playful, thoughtful, mischievous, pout, investigating,
derp, sparkle, hungry, anticipation, laugh, eager, flushed, cheeky, ecstatic).

Idle decay (each stage fires once, tunable at the top of `Eilik_Bot.ino`):

```
active → BORED (45 s) → TIRED (90 s) → YAWNING (135 s) → SLEEPING (180 s)
```

Wake chain — any touch, motion, or serial command while sleeping:

```
SLEEPING → YAWNING → SLEEPY → SMILE
```

Sleeping cuts servo torque (limp arms). To test the whole cycle fast: type `s`
to sleep, then touch a pad (or type `w`) to watch the wake chain.

---

## 6. Serial output cheat-sheet

| Line | Meaning |
|---|---|
| `emotion 17/38 FURIOUS` | current emotion + index |
| `[Gesture] Start / Step / Complete` | servo movement, per step, with speeds & timing |
| `TOUCH: HEAD/BODY (raw=… thr=…)` | which pad, live reading, threshold |
| `TOUCH: FACE (screen)` | screen tap |
| `IMU: KNOCK / SHAKE / TILT / UPRIGHT` | motion event |
| `snd: clip N for face M` | a WAV clip played instead of the synth effect |
| `[Console] play 12/38` | you triggered an emotion from Serial |

---

## Notes / known limits

- Only 3 touch surfaces exist, so datasheet combos (e.g. "chest+face within
  0.5 s → Flushed") aren't detected. Those combo-only emotions
  (LAUGH, EAGER, TEARY, DISAPPOINTED, HEARTBROKEN, FLUSHED, CHEEKY, ECSTATIC)
  are reachable via the idle fidgets, the shake chain, and always via the
  serial console (types 1–38).
- Touch thresholds are hardware/humidity dependent — calibrate with `t`.
- Continuous servos drift; tune `SERVO_STOP_TRIM_*` in `Pins.h` if a servo
  creeps at rest.
