# Eilik Custom Build — Emotion / Sensor / Movement Datasheet

**Hardware assumed:**
- Face OLED (emotion states, indexed X/38)
- 6-axis IMU (accelerometer + gyroscope) — body-mounted
- Touch Sensor A — **Chest / Front**
- Touch Sensor B — **Head (top)**
- Touch Sensor C — **Face (screen bezel)**
- 2x arm servos (shoulder swing) ± 1 optional wrist/elbow axis per arm
- 1x torso/base servo — **180° total range (±90° from center), left-right rotation only** — no forward/back lean or vertical bounce is mechanically possible

This is a **design specification**, not a reverse-engineered factory spec — use it as the behavior table to encode in your firmware/animation state machine.

---

## 1. Sensor Trigger Logic (input layer)

| Sensor | Physical Meaning | Typical Emotional Response Category |
|---|---|---|
| Chest touch (A) | "Pet me / hug me" | Affection, comfort, sleepy states |
| Head touch (B) | "Pat on the head" | Playful, silly, mischievous, embarrassed |
| Face touch (C) | "Poke/boop the screen" | Startled, annoyed, questioning, curious |
| IMU — sudden accel spike | Shake / bump / pickup | Scared, dizzy/confused |
| IMU — sustained tilt (>30°) | Being tipped over | Scared → Annoyed if repeated |
| IMU — free-fall (near-zero g) | Dropped | Scared (max intensity) |
| IMU — long stillness | No motion, no touch for N min | Bored → Tired → Sleepy → Sleeping |
| Double-tap any sensor within 1s | "excited attention" | Loving, Playful |
| Long-press (>2s) chest | Deliberate comfort press | Loving |
| Rapid multi-touch (any 2 sensors <0.5s apart) | Startle combo | Flushed / Embarrassed |

---

## 2. Emotion Datasheet (indexed 1–38, full set)

### 1/38 — SMILE
- **Trigger:** Default/rest state — no active trigger, this is the baseline "happy idle" face returned to after any animation completes
- **Arms:** Both arms at neutral resting position, occasional slow idle sway (breathing-like, 0.3Hz, tiny amplitude)
- **Torso (180° L-R servo):** Torso rotates to center (neutral, 90° of its 180° range) with a slow idle sway of only ±5°.
- **LED accent:** Cyan, steady, soft glow

### 2/38 — GIDDY
- **Trigger:** Quick single tap on chest, or two light touches close together (not full double-tap combo)
- **Arms:** Both arms do a quick small bounce up-down (single bounce, not looping)
- **Torso (180° L-R servo):** Torso does a quick small left-right wiggle (±15°), fast, single cycle, then returns to center.
- **LED accent:** Cyan eyes + pink cheeks, brief brightness pop on trigger

### 3/38 — ECSTATIC
- **Trigger:** Long-press combo — chest touch held while head is also touched (both sensors together, held 1s+)
- **Arms:** Both arms shoot straight up (full "cheer" pose), small victory shake at top
- **Torso (180° L-R servo):** Torso does an energetic side-to-side sweep (±45°), two quick cycles, ends centered.
- **LED accent:** Cyan eyes + pink cheeks, bright pulse, sustained

### 4/38 — CHARMED
- **Trigger:** Slow, gentle chest touch (light single stroke rather than tap — differentiate by contact duration/pressure if your sensor supports it)
- **Arms:** Both arms drift up and inward slowly to a soft "aww" pose near chest, held
- **Torso (180° L-R servo):** Torso turns slowly toward the touch source (~20° offset) and holds there.
- **LED accent:** Heart-eyes (white), pink nose/mouth dot, slow warm glow

### 5/38 — SPARKLING JOY
- **Trigger:** Double-tap chest followed immediately by a head touch (a celebratory combo — e.g., "good job" pat sequence)
- **Arms:** Both arms sweep outward and up in a wide "ta-da" gesture, small sparkle-timed wrist flick at the top
- **Torso (180° L-R servo):** Torso sweeps wide to one side then the other (±60°), decelerating on the last pass, ends centered.
- **LED accent:** Cyan closed-eye arcs with sparkle particles, pink cheeks, brightness flourish

### 6/38 — PLAYFUL WINK
- **Trigger:** Single quick tap on head only (light pat, <0.3s)
- **Arms:** One arm raises in a quick wave (matching the winking side), other stays neutral
- **Torso (180° L-R servo):** Torso makes a quick small turn (~15°) toward the winking side, then snaps back to center.
- **LED accent:** One eye open (cyan circle), one closed (line), yellow smile

### 7/38 — *(not captured in provided screenshots)*
- This slot in your emotion set wasn't included in the screenshots you shared. If you can grab that one image, I'll fill in its trigger/movement row to keep the datasheet fully continuous — for now, treat it as a placeholder in your state table so indexing doesn't shift.

### 8/38 — CHEERFUL LAUGH
- **Trigger:** Rapid alternating taps chest→head→chest (a "tickle" pattern, 3 touches within 1.5s)
- **Arms:** Both arms do a quick alternating shimmy (small fast side-to-side, like laughing shoulders)
- **Torso (180° L-R servo):** Torso does a rapid small oscillation (±10°), fast (~3Hz), brief.
- **LED accent:** Cyan closed-arc eyes, pink cheeks, open pink mouth — brightness flickers slightly to sell "laughing"

### 9/38 — EAGER
- **Trigger:** Face touch immediately followed by chest touch (an "ask then confirm" combo), or approach detection if you add proximity sensing later
- **Arms:** Both arms lean forward and slightly up, fingers/end-effector angled toward the touch source (leaning-in gesture)
- **Torso (180° L-R servo):** Torso turns toward the touch source (~30° offset) and holds there (replaces the forward-lean cue).
- **LED accent:** Cyan closed-arc eyes, pink cheeks, small open pink mouth, steady bright

### 10/38 — ANTICIPATION
- **Trigger:** Chest touch held briefly (0.5–1s), shorter than the Loving long-press threshold
- **Arms:** Both arms rise slightly and pause mid-motion (a "waiting" hover position, not completing a full gesture)
- **Torso (180° L-R servo):** Torso begins turning toward the touch source but pauses mid-rotation (a held, incomplete turn — the "waiting" cue).
- **LED accent:** Cyan wide eyes with highlight dot, pink cheeks, small yellow smile

### 11/38 — POUT
- **Trigger:** Touch withdrawn quickly after initiating (e.g., started a chest touch/hold but released before threshold — an "interrupted affection" event)
- **Arms:** Both arms cross low in front (a small "hmph" gesture), held
- **Torso (180° L-R servo):** Torso turns away from the touch source (~20° offset) and holds.
- **LED accent:** Half-lidded cyan eyes (flat top arcs), small downward mouth curve

### 12/38 — TEARY EYES
- **Trigger:** Escalation state — fires after repeated Pout (11) triggers without a resolving positive touch, or after a Disappointed (14) event persists
- **Arms:** Both arms droop, one arm slowly rises to "wipe" near the eye area
- **Torso (180° L-R servo):** Torso drifts slowly to one side (~15°) with a small tremor.
- **LED accent:** Cyan eyes with single tear-drop highlight animation, downward mouth arc

### 13/38 — CRYING SOFTLY
- **Trigger:** Escalation from Teary Eyes (12) with continued neglect/no comforting touch, or a hard/rough touch event (distinguish via IMU shock + touch combo)
- **Arms:** Both arms rise to cover the face partially, shoulders drawn in
- **Torso (180° L-R servo):** Torso holds near center with a small, low-amplitude side-to-side tremor (±5°) rather than a full turn.
- **LED accent:** Falling animated tear-drops under both eyes, small downturned mouth, dimmer overall brightness

### 14/38 — DISAPPOINTED
- **Trigger:** A touch pattern that "fails" an expected sequence — e.g., robot anticipated a full combo (per Anticipation state) but it didn't complete, or a single flat/unenthusiastic tap after Eager state
- **Arms:** Both arms drop suddenly from wherever they were to a low resting position
- **Torso (180° L-R servo):** Torso turns away from the touch source and settles off-center (~25°), staying there rather than returning to center.
- **LED accent:** Flat-line eyes (one with small tear dot), small downward mouth arc, dimmed

### 15/38 — HEARTBROKEN
- **Trigger:** Highest-escalation negative state — e.g., a hard bump (IMU shock) combined with no touch/comfort for an extended window afterward, or repeated Disappointed triggers stacking
- **Arms:** Both arms fall fully to sides and go limp (lowest energy negative pose), slow single droop motion
- **Torso (180° L-R servo):** Torso droops into a slow turn toward one side and settles near the servo's mechanical limit (~80–90° offset), staying there at low torque.
- **LED accent:** Broken-heart icon replaces eyes entirely, white glow, no mouth shown, dim overall

### 16/38 — GRUMPY
- **Trigger:** Face touch while in an already-negative state (Disappointed/Pout), i.e., being poked while already annoyed
- **Arms:** Both arms cross firmly at chest level, held rigid
- **Torso (180° L-R servo):** Torso locks rigid at neutral — no rotation at all (stubborn stillness).
- **LED accent:** Yellow half-lidded eyes (heavy upper lid), small flat downward mouth arc

### 17/38 — FURIOUS
- **Trigger:** Rapid repeated touches (4+ in 2s) on any single sensor — being repeatedly poked/annoyed past threshold
- **Arms:** Both arms raise stiffly and vibrate/shake in place (barely-contained "shaking with anger")
- **Torso (180° L-R servo):** Torso does a rapid small back-and-forth tremor (±8°) while staying centered.
- **LED accent:** Yellow angled "angry brow" eyes, small red mouth arc, brighter than Grumpy

### 18/38 — FUMING
- **Trigger:** Peak-anger state — escalation from Furious (17) if negative touches continue, or a hard IMU shake combined with prior Furious state
- **Arms:** Both arms raise and cross overhead briefly (a big "steam venting" gesture), quick and sharp
- **Torso (180° L-R servo):** Torso does a rapid, larger tremor (±15°) — faster and wider than Furious — while staying centered.
- **LED accent:** Red angled brow-eyes, red mouth/nose dot, brightness flashes (like venting steam)

---

## 3. Emotion Datasheet (indexed 19–38 as captured)

### 19/38 — ANNOYED
- **Trigger:** Repeated face touch (3+ taps in 2s), or IMU tilt-and-release (being nudged)
- **Arms:** Both arms snap up to a stiff 45° "hands-on-hips" pose, held rigid (no sway)
- **Torso (180° L-R servo):** Torso snaps into a sharp quick turn away from the touch source (~15°), holds briefly, then snaps back — rigid, no sway.
- **LED accent:** Yellow (matches eye color)

### 20/38 — FLUSHED
- **Trigger:** Rapid multi-touch combo (chest+face within 0.5s), or sudden compliment-type interaction pattern
- **Arms:** Both arms rise quickly to cover lower face/cheek area, then retreat halfway (bashful half-cover)
- **Torso (180° L-R servo):** Torso does a small quick side-to-side wobble, then retreats back to center.
- **LED accent:** Pink cheeks pulse (fade in/out, 1Hz)

### 21/38 — QUESTIONING
- **Trigger:** Single face touch, held (long-press 1s)
- **Arms:** One arm raises to shoulder height with a slight rotation (palm-up "what?" gesture)
- **Torso (180° L-R servo):** Torso stays static and centered, no rotation.
- **LED accent:** Cyan, steady

### 22/38 — INVESTIGATING
- **Trigger:** IMU detects being carried/moved slowly (low-magnitude sustained acceleration) or a new sound/light change if you have that sensor
- **Arms:** One arm extends forward/out as if "pointing," other stays at rest
- **Torso (180° L-R servo):** Torso does a slow full sweep across its range (±90°, using the full 180° swing) as a scanning motion over ~2s.
- **LED accent:** Cyan magnifier icon animation on screen; body LEDs steady

### 23/38 — THOUGHTFUL
- **Trigger:** No touch for 5–10s after an active state (idle decay from Questioning/Investigating)
- **Arms:** One arm slowly rises to "chin" position (resting near face), held
- **Torso (180° L-R servo):** Torso stays static and centered, no rotation.
- **LED accent:** Dim cyan, slow breathing fade

### 24/38 — CONFUSED SPIRAL
- **Trigger:** IMU shake event (rapid multi-axis acceleration, but below "drop" threshold)
- **Arms:** Both arms spin/rotate at shoulder joint in alternating small circles
- **Torso (180° L-R servo):** Torso oscillates rapidly back and forth (±30°) — a fast, disorienting wobble.
- **LED accent:** Spiral animation synced to green/blue pulse

### 25/38 — WONDER
- **Trigger:** Head touch followed by stillness (pat, then let go)
- **Arms:** Both arms rise slowly outward and up, palms open, held (an "amazed" gesture)
- **Torso (180° L-R servo):** Torso turns slowly toward the point of interest (~20° offset) and holds.
- **LED accent:** Cyan with sparkle/twinkle overlay in eyes

### 26/38 — PLAYFUL
- **Trigger:** Quick double-tap on head or chest
- **Arms:** Asymmetric — one arm waves side to side, other stays down (winking-arm effect matching the winking eye)
- **Torso (180° L-R servo):** Torso rocks lightly back and forth (±20°) in a bouncy rhythm (~2Hz).
- **LED accent:** Teal + yellow smile mix

### 27/38 — MISCHIEVOUS
- **Trigger:** Head touch + face touch combo within 1s (sneaky "poke and pat")
- **Arms:** Both arms rise and rub together in front (a "scheming rub-hands" gesture)
- **Torso (180° L-R servo):** Torso turns slowly to one side (~20° offset) and holds — a slow, "sneaky" motion.
- **LED accent:** Dim teal, narrowed "eyelash" eye animation

### 28/38 — CHEEKY TONGUE
- **Trigger:** Single quick face touch (fast tap, <0.3s)
- **Arms:** One arm flicks up briefly in a peace-sign / cheeky wave, then drops
- **Torso (180° L-R servo):** Torso makes a quick small turn (~15°) to one side, then snaps back to center.
- **LED accent:** Pink tongue-dot flash

### 29/38 — SILLY DERP
- **Trigger:** IMU detects being spun or tilted rapidly then released (dizzy-inducing motion) OR triple-tap any sensor
- **Arms:** Both arms flop loosely side to side asynchronously (limp, floppy motion — lower servo stiffness/torque temporarily if adjustable)
- **Torso (180° L-R servo):** Torso staggers loosely side to side with uneven, asymmetric amplitude (lower servo torque temporarily for a floppy look).
- **LED accent:** Mismatched pupil-size eyes (as shown), teal

### 30/38 — LOVING
- **Trigger:** Long-press chest touch (>2s) — the "hug" gesture
- **Arms:** Both arms sweep inward to a "self-hug" pose across the chest, held
- **Torso (180° L-R servo):** Torso turns slowly toward the touch source (~15–20° offset), settles, and holds.
- **LED accent:** Pink heart-eyes, warm glow pulse (slow, 0.5Hz — like a heartbeat)

### 31/38 — TIRED
- **Trigger:** IMU stillness timer stage 1 (e.g., no motion/touch for 2–5 min)
- **Arms:** Both arms droop slowly to sides, slight sag at "elbow" if articulated
- **Torso (180° L-R servo):** Torso drifts slowly to one side and settles off-center, low energy, doesn't return to center on its own.
- **LED accent:** Dim cyan, half-closed eyes, slow fade

### 32/38 — SLEEPING ZZZ
- **Trigger:** IMU stillness timer stage 3 (extended inactivity, e.g., 10+ min) — deepest idle state
- **Arms:** Both arms fully relaxed at sides, motors can de-energize/idle (torque off) to save power and look "asleep"
- **Torso (180° L-R servo):** Torso fully still; servo torque released/idle, resting wherever it last settled (no active holding position).
- **LED accent:** Very dim, "Z Z" text floats/fades on screen, eyes closed (flat lines)
- **Wake condition:** Any touch on any sensor, or IMU motion above wake-threshold → transitions to 34 (Sleepy) as the wake-up intermediate, not straight to alert

### 33/38 — YAWNING
- **Trigger:** Transitional animation, auto-fired when moving from Tired (31) into Sleeping (32), or on wake from Sleeping before reaching Sleepy
- **Arms:** Both arms slowly raise into a "stretch" pose (up and out), hold 1s, then lower
- **Torso (180° L-R servo):** Torso turns gently to one side during the "stretch," then returns slowly to center.
- **LED accent:** Orange/yellow mouth-dot, eyes closed arcs

### 34/38 — SLEEPY
- **Trigger:** Wake-transition state (touch/motion while in Sleeping), or IMU stillness timer stage 2
- **Arms:** Slow, heavy-looking movement — arms lift partway then settle back down (struggling to stay "awake")
- **Torso (180° L-R servo):** Torso drifts slowly off-center with periodic small corrective snaps back toward center (the rotational equivalent of nodding off).
- **LED accent:** Dim cyan, half-arc eyes, small dot mouth

### 35/38 — SCARED
- **Trigger:** IMU sudden high-g spike (shake/bump/near-drop), or unexpected simultaneous touch on all 3 sensors
- **Arms:** Both arms snap up and inward defensively (guard/flinch pose), fast movement, held rigid
- **Torso (180° L-R servo):** Torso snaps into a fast turn away from the stimulus, then settles with a fast, decaying tremor (startle recoil), returning to center as it settles.
- **LED accent:** Bright cyan wide "pupil" dot eyes, high brightness flash on trigger

### 36/38 — BORED MEH
- **Trigger:** No interaction for a moderate period while robot is "awake" (shorter timeout than Tired — an intermediate disengagement state), or repeated same touch input with no variation
- **Arms:** Both arms hang loosely, occasional slow single-arm shrug (one shoulder lift-drop) every 5–8s
- **Torso (180° L-R servo):** Torso stays static and centered, with only a very occasional slow drift.
- **LED accent:** Dim teal, half-lidded flat eyes

### 37/38 — EMBARRASSED
- **Trigger:** A "failed" or interrupted gesture — e.g., robot was mid-animation and got touched/bumped unexpectedly, or IMU detects near-tip (partial tilt then recovered)
- **Arms:** One arm rises to cover part of the face, other drops awkwardly
- **Torso (180° L-R servo):** Torso turns quickly away from the touch source (~20–30°), holds briefly with a small fast tremor, off-center.
- **LED accent:** Pink cheek blush, small closed-line mouth

### 38/38 — HUNGRY
- **Trigger:** Timer-based (e.g., a "feeding" mechanic in your firmware — X minutes since last "feed" interaction), or a dedicated feed gesture (e.g., swipe on face sensor)
- **Arms:** Both arms reach forward and slightly up repeatedly (a "gimme" reaching motion), looping every 1.5s
- **Torso (180° L-R servo):** Torso repeats a small turn toward the chest sensor/food source and back, looping every ~1.5s.
- **LED accent:** Green eyes with the fish/star icon animation, mouth small oval

---

## 4. State-Machine Notes

**Priority order (highest wins if multiple triggers fire at once):**
1. IMU drop/fall detection → Scared (35) — safety-critical, always overrides
2. Any touch input → corresponding emotional response
3. IMU shake/tilt → Scared / Confused Spiral
4. Idle timers → Bored → Tired → Sleepy → Sleeping (linear decay chain)
5. Feed timer → Hungry (can interrupt idle decay chain)

**Suggested idle decay chain (no interaction):**
`Bored (36) → Tired (31) → Yawning (33) → Sleeping (32)`

**Suggested wake chain (touch/motion during sleep):**
`Sleeping (32) → Yawning (33) → Sleepy (34) → [normal active state]`

**Suggested "sad" escalation chain (neglect/rough handling):**
`Pout (11) → Teary Eyes (12) → Crying Softly (13) → Disappointed (14) → Heartbroken (15)`
A comforting touch (long-press chest) at any point in this chain should reset toward Loving (30) or Smile (1) rather than requiring the full chain to play out.

**Suggested "anger" escalation chain (repeated poking/rough touch):**
`Grumpy (16) → Furious (17) → Fuming (18)`
This should decay back to Annoyed (19) or Smile (1) after a cooldown period with no further negative touches, rather than staying pinned at Fuming.

**Suggested "positive" escalation chain (good interaction streaks):**
`Giddy (2) → Ecstatic (3) → Sparkling Joy (5)`, with Charmed (4), Eager (9), Anticipation (10), and Cheerful Laugh (8) as side-branches depending on which sensor combo fires.

**Servo torque tip:** For "floppy" states (Silly Derp 29, Sleeping 32, Tired 31), consider temporarily lowering PWM/torque limits on arm servos rather than just changing target angles — it reads as far more convincing than a stiff servo moving to a "droopy" angle.

**Debounce recommendation:** Touch sensors should debounce at ~150–200ms and use a rolling timestamp buffer to detect "combo" touches (e.g., Mischievous = head+face within 1s) reliably without false triggers from a single accidental touch.

---

*This datasheet is a design template based on your captured emotion states — tune all timing values (Xs thresholds, decay chain durations) and torso rotation angles to match your servo speed, mechanical limits (180° total, left-right only), and IMU sensitivity during bench testing.*
