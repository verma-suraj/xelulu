# Eilik — Final Hardware Acceptance Test

Every trigger below is read directly out of the flashed firmware
(`Eilik_Master_Combo`), not from the datasheet. If the robot does what this
document says, the branch is good to merge.

**Serial monitor: 115200 baud.** Every reaction prints, so you can check
behaviour even when a movement is subtle.

---

## 0. Before you start

1. **Stand the robot upright on the desk and take your hands off it.**
2. Power on / reset.
3. Check these four boot lines:

```
Eilik_Master_Combo boot
MPU6050 ok (robot-frame axes)                     <- NOT "not found"
[IMU] level captured (0.0x,0.0x,1.00) - tilt ready  <- Z must be ~1.00
[touch] GPIO32 baseline=~1400 -> press below ~1210
[touch] GPIO14 baseline=~1330 -> press below ~1150
```

> **If Z is not ≈ 1.00** (e.g. `0.31,0.36,0.88`) the robot was tilted or held at
> power-on. Tilt is then measured against a crooked pose and **test 5.1 will
> fail**. Sit it flat and reset.
>
> Both the tilt reference and the touch baselines are learned **once at boot**,
> hands off.

4. Boot reaction: face **SMILE**, "hello" voice clip, and an **ECSTATIC** body
   move — both arms swing up high, wiggle, torso swings — ending with
   `[body] done (~1450ms)`.

---

## 1. Head pad (GPIO32, pad on top of head)

Counters reset after **10 s** with no head touch. Let it idle 10 s between blocks.

| # | Do this | Serial | Face | Body movement |
|---|---|---|---|---|
| 1.1 | Single tap (short) | `TOUCH: HEAD` → `emotion 6/38 PLAYFUL WINK` | **Wink** | `[body] WINK: 4 steps` — **right arm only** waves twice, small torso turn (~0.6 s) |
| 1.2 | Two fast taps (**< 0.4 s apart**) | `emotion 26/38 PLAYFUL` | **Playful** | `[body] PLAYFUL: 6 steps` — right arm waves 3× (~1.0 s) |
| 1.3 | Tap **4 times** within 10 s | 4th tap → `emotion 37/38 EMBARRASSED` | **Embarrassed** | `[body] EMBARRASSED: 5 steps` — left arm up / right arm down (hiding), torso turns away, holds 1.2 s, returns (~1.9 s) |

> Taps 1–3 give Wink/Playful; from the **4th** onward it is Embarrassed until
> you leave it alone for 10 s.

---

## 2. Chest pad (GPIO14, belly/back pad) — hold-duration ladder

The reaction is chosen on **release**, by how long you held it.

| # | Hold for | Serial | Face | Body movement |
|---|---|---|---|---|
| 2.1 | Quick tap (**< 0.25 s**) | `emotion 2/38 GIDDY` | **Giddy** | `[body] GIDDY: 4 steps` — both arms bounce up/down, torso right-left (~0.6 s) |
| 2.2 | Two quick taps (**< 0.4 s apart**) | `emotion 26/38 PLAYFUL` | **Playful** | right arm waves 3× (~1.0 s) |
| 2.3 | **0.25 – 0.8 s** | `emotion 10/38 ANTICIPATION` | **Anticipation** | `[body] ANTICIPATION: 3 steps` — both arms rise, **hold 1.5 s**, lower (~2.0 s) |
| 2.4 | **0.8 – 2 s** | `emotion 4/38 CHARMED` | **Charmed** | `[body] CHARMED: 3 steps` — arms rise gently, **hold 1.4 s**, lower (~2.4 s) |
| 2.5 | **> 2 s** (long hug) | `emotion 30/38 LOVING` | **Loving** | `[body] LOVING: 3 steps` — both arms up in a hug, **hold 2 s**, lower (~2.8 s) |

---

## 3. Screen / face poke (tap the round display)

Chain resets after **8 s** with no poke. Pokes are debounced to **600 ms** — poke
deliberately, not in a flurry.

| # | Poke number | Serial | Face | Body movement |
|---|---|---|---|---|
| 3.1 | 1st | `TOUCH: FACE` → `emotion 28/38 CHEEKY TONGUE` | **Cheeky tongue** | `[body] CHEEKY: 4 steps` — right arm up, torso turn, pause, arm down (~0.7 s) |
| 3.2 | 2nd | `emotion 21/38 QUESTIONING` | **Questioning** | `[body] QUESTIONING: 3 steps` — **right arm only** up, hold 1.5 s, down (~2.0 s) |
| 3.3 | 3rd & 4th | `emotion 19/38 ANNOYED` | **Annoyed** | `[body] ANNOYED: 5 steps` — arms up, torso left, pause, torso right, pause, arms down (~1.8 s) |
| 3.4 | 5th | `emotion 16/38 GRUMPY` | **Grumpy** | `[body] GRUMPY: 3 steps` — arms up, **hold 2 s**, down (~2.5 s) |
| 3.5 | 6th | `emotion 17/38 FURIOUS` | **Furious** | `[body] FURIOUS: 6 steps` — arms up high, angry shaking (~0.8 s) |
| 3.6 | 7th and beyond | `emotion 18/38 FUMING` | **Fuming** | `[body] FUMING: 6 steps` — arms up highest, hard shaking (~1.1 s) |

---

## 4. Two-sensor combos

These **override** the single-pad reactions above, and the pad release that
follows is deliberately swallowed (you get the combo, not a second reaction).

| # | Do this | Serial | Face | Body movement |
|---|---|---|---|---|
| 4.1 | **Hold chest**, then tap head | `emotion 3/38 ECSTATIC` | **Ecstatic** | `[body] ECSTATIC: 6 steps` — both arms swing up high, wiggle, torso swings (~1.4 s) |
| 4.2 | **Hold head**, then tap chest | `emotion 3/38 ECSTATIC` | **Ecstatic** | same as 4.1 |
| 4.3 | Touch head, then **tap the screen within 0.8 s** (or hold head while poking) | `emotion 27/38 MISCHIEVOUS` | **Mischievous** | `[body] MISCHIEVOUS: 6 steps` — arms up, small alternating wiggles, down (~1.2 s) |
| 4.4 | Touch chest, then **tap the screen within 0.8 s** (or hold chest while poking) | `emotion 20/38 FLUSHED` | **Flushed** | `[body] FLUSHED: 5 steps` — arms up, quick wiggle (~1.0 s) |

> For a **clean** poke-chain test (§3), keep off both pads for 0.8 s first —
> otherwise you get 4.3/4.4 instead.

---

## 5. Motion / IMU tests

| # | Do this | Serial | Face | Body movement |
|---|---|---|---|---|
| 5.1 | **Lift and tilt** past ~45° for ~0.5 s | `IMU: TILT (picked up)` → `emotion 35/38 SCARED` | **Scared** (held up to 60 s) | `[body] SCARED: 6 steps` — arms fly up, torso jerks, settles (~1.5 s) |
| 5.2 | Put it back down flat | `IMU: UPRIGHT (put down)` → `emotion 2/38 GIDDY` | **Giddy** (relieved) | arms bounce, torso right-left (~0.6 s) |
| 5.3 | Tilt **twice within 8 s** | 2nd → `emotion 19/38 ANNOYED` | **Annoyed** | arms up, torso left-right, down (~1.8 s) |
| 5.4 | **Shake** it — 1st time | `IMU: SHAKE` → `emotion 24/38 CONFUSED SPIRAL` | **Confused spiral** | `[body] CONFUSED: 6 steps` — arms counter-swing, torso swings (~1.1 s) |
| 5.5 | Shake again (within 12 s) | `emotion 35/38 SCARED` | **Scared** | arms fly up, torso jerks (~1.5 s) |
| 5.6 | Shake a 3rd time (within 12 s) | `emotion 13/38 CRYING SOFTLY` | **Crying softly** | `[body] CRYING: 6 steps` — arms up, torso rocks (~1.8 s) |
| 5.7 | **Knock the desk** next to it | `IMU: KNOCK` → `emotion 25/38 WONDER` | **Wonder** | `[body] WONDER: 3 steps` — arms rise slowly, hold 1.6 s, lower (~2.5 s) |

> There is a **1.5 s cooldown** after a shake and an **0.8 s settle window**
> after every body movement during which IMU events are ignored on purpose —
> the servos shake the robot and it used to startle at itself. Let it finish
> moving before testing 5.4–5.7.

---

## 6. Idle life, wind-down and sleep (leave it completely alone)

| # | Time with no interaction | What should happen |
|---|---|---|
| 6.1 | every **7–16 s** | A random "fidget" — one of 18 fun faces + its body move. Never a sleepy face. |
| 6.2 | after **35 s** | `[decay] wind-down cycle A/B/C` — one of three random wind-downs (bored / thoughtful / pout → tired → **yawning**) |
| 6.3 | end of the cycle | `emotion 32/38 SLEEPING ZZZ` — flat eyes + floating Zzz, **servos de-energise (go limp)** |
| 6.4 | touch it while asleep | `[wake] awake` → SMILE, **then the reaction for whatever you did** (no yawn on wake) |

**Every touch reaction holds for a random 5–10 s after you lift your finger**,
then returns to **SMILE** by itself. Measured on the device: 8.35 s (Cheeky),
5.47 s (Wink), 8.13 s (Loving), 6.01 s (Anticipation). The duration is random by
design, so the same touch twice will not hold for the same length of time.

The wind-down steps (§6.2) keep their own fixed pacing — falling asleep should
feel gradual, not random.

---

## 7. Things that are correct, not bugs

* **The same trigger sometimes makes a different sound.** 15 faces have a voice
  clip and use it **~50 % of the time at random** — Giddy/Laugh→"risa",
  Grumpy/Annoyed/Cheeky→"bahhh", Furious→"arghh", Questioning→"sus",
  Confused→"what", Wonder/Derp→"oi", Playful/Mischievous→"hehe",
  Yawning→"pigyawn", Starry→"soaryn". Watch for `snd: clip N for face M`.
* **Idle fidgets are random** — you will not get the same sequence twice.
* **The wind-down path is random** — cycle A, B or C.
* **A new sound cuts off the old one.** It now fades out over ~8 ms instead of
  clicking, but truncation itself is intended: the newest emotion wins.
* **Smile and Sleeping have no body movement** by design.
* A brief servo twitch at power-on is normal (GPIO15 is a strap pin).

---

## 8. Pass / fail

**PASS = merge** if:

- [ ] All of §1–§4 produce the listed face **and** the listed body movement
- [ ] §5 IMU tests fire (after a correct upright boot, §0)
- [ ] §6 reaches `SLEEPING ZZZ` on its own and wakes on touch
- [ ] It runs **5+ minutes without freezing** — serial keeps printing
- [ ] No `Guru Meditation` and no unexpected `rst:0x` in the log
- [ ] No `IMU: SHAKE` or `TOUCH:` lines while nobody is touching it

### If something fails

| Symptom | Knob |
|---|---|
| Pad never triggers / triggers by itself | Was it touched during boot? Reset hands-off. Otherwise `TOUCH_DROP` / `RELEASE_DROP` in `EilikTouch.cpp` |
| Tilt never fires | Boot Z ≠ 1.00 → reset upright (§0). Else `TILT_DEG` in `Pins.h` |
| Shake too easy / too hard | `SHAKE_SPIKE_G` in `Pins.h` |
| Robot startles at its own movement | raise `SELF_MOTION_SETTLE_MS` in `EilikMotion.cpp` |
| Arms move the wrong way | `SERVO_DIR[3]` in `EilikServos.cpp` |
| Arms drift when idle | `SERVO_STOP_TRIM_*` in `Pins.h` |
| Arms droop / weak on the down stroke | `REV_GAIN[3]` in `EilikServos.cpp` |
| `[IMU] I2C bus stuck` appears | Bus wiring/pull-ups — see `debug.md` §6. Robot keeps running; only the IMU pauses |
| Audio crackles while servos move | Power, not firmware — `debug.md` §6 |
