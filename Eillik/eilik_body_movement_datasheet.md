# Eilik-Style Robot — Body & Arm Movement Datasheet

Companion to the facial expression sheets. Covers how the **body** (head tilt/turn,
two arms, torso/base rotation) moves and how that motion pairs with the eye
expressions, so you can hand this straight to an AI coding assistant alongside
the facial JSON schema.

Grounded in Eilik's actual hardware: it uses **4 identical custom servos**
(Energize Lab's "EM3"/"EM3A" motor — up to 3kg torque, position/voltage/
temperature/current feedback, and an overload-clutch that lets a joint be
gently hand-turned without damage). Those 4 servos are allocated one each to:
**head, left arm, right arm, and torso/base rotation.** The manual itself
describes power-on as "rotational movements of head, arms, and body" — that's
the whole vocabulary of motion; every emotion is just a different combination
of those four angles played at a different speed.

---

## 1. The Four Motors — What Each One Actually Does

| Servo | Joint | Motion type | Practical range | Notes |
|---|---|---|---|---|
| M1 | **Head** | Turn (yaw) left/right on the neck, plus a small forward/down nod from a spring-loaded tilt in the neck mount | Yaw: ±45\u00b0 · Nod: 0\u00b0 (up) to 30\u00b0 (drooped) | Fastest-reacting motor — it's what "looks at" a touch or sound source first |
| M2 | **Left arm** | Single rotation around the shoulder pivot, like a clock hand | 0\u00b0 (hanging down) to 180\u00b0 (straight up) | Also double-duty as a manual input — a person can lift/lower it by hand thanks to the overload clutch, used in Settings mode |
| M3 | **Right arm** | Mirror of M2 | 0\u00b0 to 180\u00b0 | Same dual input/output behavior |
| M4 | **Torso / base** | Rotates the whole body left/right on the base | ±180\u00b0 continuous-capable | Used for "look around," dizzy spins, and the fishing/twist mini-game |

There is **no dedicated up/down bounce motor** — the "bouncy," "wiggly,"
"breathing" look reviewers describe is faked by rapidly cycling small head-nod
and torso-lean amounts, not a vertical lift. Keep that in mind: any "bounce" in
your code should be a fast oscillation of M1/M4 angles, not a Z-axis
translation.

---

## 2. How Body Language Pairs With a Facial Expression

Three rules the real robot follows, worth copying:

1. **Head moves first, everything else follows.** On any trigger (touch,
   sound, shake), M1 reacts within ~100ms to orient toward the stimulus, 1–2
   frames before the eyes change and well before the arms/torso move. This is
   what makes it feel alive rather than "playing an animation."
2. **Arms amplify, they don't lead.** The eyes carry the actual emotion; the
   arms just add physical emphasis (thrown up for excitement, drooped for
   sadness, pulled in for fear). If you only had time to animate one thing,
   animate the eyes — arms are the multiplier, not the message.
3. **Torso rotation = intent to engage/disengage.** Body turned to face
   forward = attentive/interactive. Body rotated away = bored/ignoring.
   Continuous spin = dizzy/overloaded. This is the one axis that reads as
   "attention," independent of whatever the face is doing.

---

## 3. Pose Table — Body Params Per Expression

Angles use the convention: **arm 0\u00b0 = hanging straight down, 180\u00b0 =
straight up**; **head_yaw 0\u00b0 = facing forward, + = turned right**;
**head_nod 0\u00b0 = level, + = drooped forward/down**; **torso_lean 0\u00b0 =
upright, + = leaning forward**; **torso_yaw** same convention as head_yaw.

| Expression | L-arm | R-arm | head_yaw | head_nod | torso_lean | torso_yaw | Motion notes |
|---|---|---|---|---|---|---|---|
| Neutral / Idle | 10\u00b0 | 10\u00b0 | 0\u00b0 | 0\u00b0 | 0\u00b0 | slow drift ±15\u00b0 | Everything idles on a slow 4-8s sine cycle |
| Happy | 130\u00b0 | 130\u00b0 | 0\u00b0 | 0\u00b0 | 0\u00b0 | 0\u00b0 | quick double-bounce: nod 0\u2192-10\u21920 twice |
| Excited | 160\u00b0 | 60\u00b0 (alternating) | oscillate ±20\u00b0 | 0\u00b0 | 0\u00b0 | oscillate ±30\u00b0 | arms alternate up/down fast (~4 Hz), whole body wiggles |
| Sad | -15\u00b0 | -15\u00b0 | 0\u00b0 | 25\u00b0 | 12\u00b0 | 0\u00b0 | slow, heavy easing; arms hang past rest, limp |
| Angry / Cross | 90\u00b0 | 90\u00b0 | 0\u00b0 | -5\u00b0 (jut up) | -8\u00b0 (lean back/stiff) | rapid jitter ±5\u00b0 | sharp, fast attack on each move, no easing |
| Frustrated | 70\u00b0 | 70\u00b0 | small shake ±8\u00b0 | 0\u00b0 | 0\u00b0 | rapid jitter ±8\u00b0 | fast small shudder loop, ~6 Hz |
| Surprised | 100\u00b0 (snap) | 100\u00b0 (snap) | 0\u00b0 | -10\u00b0 (head back) | -10\u00b0 | 0\u00b0 | single fast snap (80ms), then hold |
| Scared | 30\u00b0 (pulled in) | 30\u00b0 (pulled in) | 0\u00b0 | 15\u00b0 | 15\u00b0 (shrink/hunch) | tremble ±10\u00b0 | continuous fine tremble on all axes |
| Curious | 10\u00b0 | 140\u00b0 (one up) | 20\u00b0 (cocked) | 0\u00b0 | 5\u00b0 toward raised arm | 0\u00b0 | slow, inquisitive easing, hold 2s |
| Sleepy | -10\u00b0 | -10\u00b0 | 0\u00b0 | 30\u00b0, drifting to 40\u00b0 | 15\u00b0 | 0\u00b0 | very slow droop cycle, occasional head snap-back to 0\u00b0 |
| Dizzy | 45\u00b0 | 45\u00b0 (loose) | wobble ±15\u00b0 | wobble ±10\u00b0 | 0\u00b0 | continuous 360\u00b0 spin | torso_yaw spins continuously while limbs go loose/floppy |
| Embarrassed | 10\u00b0 | 10\u00b0 | 30\u00b0 (look away) | 15\u00b0 | 0\u00b0 | 20\u00b0 (turned aside) | slow turn away, small fidget |
| Crying | 150\u00b0 | 150\u00b0 (both up near face) | 0\u00b0 | 20\u00b0 | 10\u00b0 (hunched) | 0\u00b0 | arms rise to face fast then tremble in place |
| Loving | 120\u00b0 (open wide) | 120\u00b0 (open wide) | 0\u00b0 | -5\u00b0 | -8\u00b0 (leaning in) | 0\u00b0 | slow, soft easing, arms open like inviting a hug |

---

## 4. Animation & Timing Principles

1. **Easing per emotion, not one global curve.** Soft emotions (sad, sleepy,
   loving, curious) use slow ease-in-out (400–800ms). Sharp emotions (angry,
   surprised, frustrated) use fast ease-out with almost no ease-in — they
   should look "snapped into," not eased into.
2. **Micro-jitter vs. macro-pose are separate layers.** A pose (e.g. "angry
   arms at 90\u00b0") is the base position; jitter/tremble/wobble is a small
   secondary sine or noise offset added on top every frame. Don't bake jitter
   into the target pose — layer it, so you can dial intensity independently
   (mood scale below can raise jitter amplitude as anger deepens).
3. **Idle breathing loop.** At Neutral, animate a continuous, very slow
   (4–8s) sine wave on head_nod (±3\u00b0) and torso_lean (±2\u00b0) so the robot
   never looks frozen even with no trigger.
4. **Interrupt priority.** Touch/shake/sound triggers cut in immediately over
   idle animation, play the reaction pose (table above), hold, then ease back
   to Neutral — same interrupt model as the facial expression state machine.
5. **Mood as a continuous scalar.** Keep a float from -1 (angry/sad) to +1
   (happy/excited) that interactions nudge and that decays to 0 over time.
   Blend the *closest two* rows in the pose table by that scalar rather than
   hard-switching between poses — this is what makes transitions look like
   genuine mood shifts instead of a slideshow.
6. **Sync eyes and body on the same clock.** Fire the facial expression change
   and the body pose change from the same trigger event, but let the head
   (M1) start moving ~100ms before the eyes update, and let the arms (M2/M3)
   start ~100-150ms after the eyes update — this stagger is what sold the
   "alive" feeling in rule 1 above.

---

## 5. Machine-Readable Schema (feed this to your AI coding tool)

```json
{
  "servo_map": {
    "M1_head":  { "motions": ["yaw", "nod"], "yaw_range_deg": [-45, 45], "nod_range_deg": [0, 30] },
    "M2_left_arm":  { "motions": ["rotate"], "range_deg": [-20, 180], "note": "0=hang down, 180=straight up; also manual input capable" },
    "M3_right_arm": { "motions": ["rotate"], "range_deg": [-20, 180], "note": "mirror of M2" },
    "M4_torso": { "motions": ["yaw", "lean"], "yaw_range_deg": [-180, 180], "lean_range_deg": [-15, 20], "note": "yaw can spin continuously for dizzy" }
  },
  "poses": {
    "neutral":     { "l_arm":10,  "r_arm":10,  "head_yaw":0,  "head_nod":0,  "torso_lean":0,  "torso_yaw":"idle_drift_15", "easing":"slow" },
    "happy":       { "l_arm":130, "r_arm":130, "head_yaw":0,  "head_nod":0,  "torso_lean":0,  "torso_yaw":0, "easing":"medium", "extra":"double_bounce" },
    "excited":     { "l_arm":160, "r_arm":60,  "head_yaw":"oscillate_20", "head_nod":0, "torso_lean":0, "torso_yaw":"oscillate_30", "easing":"fast", "extra":"alternate_arms_4hz" },
    "sad":         { "l_arm":-15, "r_arm":-15, "head_yaw":0,  "head_nod":25, "torso_lean":12, "torso_yaw":0, "easing":"slow_heavy" },
    "angry":       { "l_arm":90,  "r_arm":90,  "head_yaw":0,  "head_nod":-5, "torso_lean":-8, "torso_yaw":"jitter_5", "easing":"snap" },
    "frustrated":  { "l_arm":70,  "r_arm":70,  "head_yaw":"shake_8", "head_nod":0, "torso_lean":0, "torso_yaw":"jitter_8", "easing":"snap_fast" },
    "surprised":   { "l_arm":100, "r_arm":100, "head_yaw":0,  "head_nod":-10,"torso_lean":-10,"torso_yaw":0, "easing":"snap_80ms" },
    "scared":      { "l_arm":30,  "r_arm":30,  "head_yaw":0,  "head_nod":15, "torso_lean":15, "torso_yaw":0, "easing":"tremble_continuous" },
    "curious":     { "l_arm":10,  "r_arm":140, "head_yaw":20, "head_nod":0,  "torso_lean":5,  "torso_yaw":0, "easing":"slow_inquisitive" },
    "sleepy":      { "l_arm":-10, "r_arm":-10, "head_yaw":0,  "head_nod":"drift_30_to_40", "torso_lean":15, "torso_yaw":0, "easing":"very_slow" },
    "dizzy":       { "l_arm":45,  "r_arm":45,  "head_yaw":"wobble_15", "head_nod":"wobble_10", "torso_lean":0, "torso_yaw":"spin_continuous", "easing":"loose" },
    "embarrassed": { "l_arm":10,  "r_arm":10,  "head_yaw":30, "head_nod":15, "torso_lean":0,  "torso_yaw":20, "easing":"slow" },
    "crying":      { "l_arm":150, "r_arm":150, "head_yaw":0,  "head_nod":20, "torso_lean":10, "torso_yaw":0, "easing":"fast_then_tremble" },
    "loving":      { "l_arm":120, "r_arm":120, "head_yaw":0,  "head_nod":-5, "torso_lean":-8, "torso_yaw":0, "easing":"slow_soft" }
  },
  "timing": {
    "head_lead_ms": 100,
    "arm_lag_ms": 125,
    "idle_breathing_period_ms": [4000, 8000],
    "interrupt_priority": ["touch", "shake", "sound", "idle"]
  },
  "mood_scalar": {
    "range": [-1, 1],
    "decays_to_zero": true,
    "blend_rule": "interpolate_between_two_closest_pose_rows"
  }
}
```

---

## 6. Build Notes

- Write one function `set_pose(l_arm, r_arm, head_yaw, head_nod, torso_lean, torso_yaw)`
  that all 14 expressions call with different numbers — same trick as the
  facial renderer: one function, many parameter sets, not 14 hand-built
  animations.
- Layer jitter/tremble/wobble as a small additive sine/noise term evaluated
  every frame on top of whichever pose is currently targeted, so intensity
  can scale with the mood value without rewriting the base pose.
- If your physical arms only have a single rotational DOF (no elbow, like
  the real Eilik), don't try to fake a second joint — the "hug/cover face"
  and "cross arms" looks are both achieved with a single shoulder rotation
  bringing the arm up and inward, not a bent elbow.
