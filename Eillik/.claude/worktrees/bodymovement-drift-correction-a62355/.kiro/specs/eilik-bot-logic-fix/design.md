# Eilik Bot Logic Fix — Bugfix Design

## Overview

The Eilik_Bot main firmware has 9 logic bugs where the integrated state machine diverges from the Eilik_Emotion_Movement_Datasheet.md and Requirements.md specifications. These bugs span touch threshold misconfiguration, missing serial debug output for gestures, incorrect state transition chains (wake, idle decay, anger escalation), wrong emotion mapping for body touch, a floating-point precision issue in the face renderer, and potential I2C bus contention. The fix approach is to correct each defective code path to match the specification while preserving all correctly-working subsystems (face rendering, sound, IMU detection, servo gestures).

## Glossary

- **Bug_Condition (C)**: The set of conditions (inputs/states) that trigger one of the 9 incorrect behaviors in the firmware
- **Property (P)**: The desired correct behavior per the datasheet/Requirements.md when those conditions are met
- **Preservation**: Existing behaviors (38 face animations, sound system, IMU detection, servo gestures, screen tap detection) that must remain unchanged by the fix
- **touchPinsUpdate()**: Function in `EilikTouch.cpp` that reads ESP32 capacitive touch pads and returns TOUCH_HEAD_PAT or TOUCH_BODY events
- **gesturePlayFace()**: Function in `EilikServos.cpp` that starts the servo gesture sequence for a given emotion
- **gestureUpdate()**: Non-blocking servo sequencer called every loop iteration
- **wakeIfAsleep()**: Function in `Eilik_Bot.ino` that handles the sleep-to-wake transition
- **react()**: Function in `Eilik_Bot.ino` that sets face, sound, and gesture for an emotion
- **Idle decay chain**: The sequence of states the robot transitions through when no interaction occurs
- **Wake chain**: The sequence of states from Sleeping back to active
- **Anger escalation**: The sequence of face expressions triggered by repeated screen pokes

## Bug Details

### Bug Condition

The bugs manifest across 9 distinct conditions in the integrated firmware. The state machine, threshold values, escalation chains, and debug output diverge from the datasheet specification.

**Formal Specification:**
```
FUNCTION isBugCondition(input)
  INPUT: input of type {event: TouchEvent|MotionEvent|ScreenTap|IdleTimer|GestureStart|I2CAccess|ConfusedRender}
  OUTPUT: boolean
  
  RETURN (input.event == TOUCH_BODY AND threshold_used == 1325)           // Bug 1.1
         OR (input.event == TOUCH_HEAD AND threshold_used == 1000)        // Bug 1.2
         OR (input.event == GESTURE_START AND no_serial_output)           // Bug 1.3
         OR (input.state == SLEEPING AND any_wake_trigger 
             AND next_state == FACE_WONDER)                               // Bug 1.4
         OR (input.idle_elapsed AND decay_skips_BORED)                    // Bug 1.5
         OR (input.event == SCREEN_TAP AND poke_count >= 5
             AND anger_order != [annoyed,grumpy,furious,fuming])          // Bug 1.6
         OR (input.event == TOUCH_BODY AND reaction == FACE_CHEEKY)      // Bug 1.7
         OR (input.face == FACE_CONFUSED AND rot_unbounded)              // Bug 1.8
         OR (input.event == I2C_ACCESS AND no_bus_coordination)           // Bug 1.9
END FUNCTION
```

### Examples

- **Bug 1.1**: User lightly touches body pad with reading 1340 — system reads as untouched (threshold 1325 < 1340 means not triggered) when it should detect (threshold 1350 > 1340 means triggered)
- **Bug 1.4**: Robot is sleeping, user taps head → immediately shows FACE_WONDER instead of transitioning through Yawning → Sleepy → active
- **Bug 1.5**: Robot idle for 60 seconds → transitions directly from active fidgets to yawn, never showing FACE_BORED
- **Bug 1.6**: User pokes screen 7 times → sequence is wink→wink→annoyed→annoyed→grumpy→fuming→furious, but should be wink→wink→annoyed→annoyed→grumpy→furious→fuming
- **Bug 1.7**: User touches body/chest pad → shows FACE_CHEEKY (tongue out), but datasheet says chest touch = affection (giddy/charmed/loving)
- **Bug 1.8**: FACE_CONFUSED active for 10+ minutes → `rot` variable grows to thousands, causing visual jitter in spiral rendering

## Expected Behavior

### Preservation Requirements

**Unchanged Behaviors:**
- All 38 face expressions must continue rendering at ~40fps with breathing bob, organic blinks, and per-face scripted animations
- I2S audio synthesis on Core 0 must continue producing all 38 emotion sounds and WAV clips non-blockingly
- MPU6050 shake detection (3+ spikes within 800ms above 0.55g) must continue triggering confused/furious/crying escalation
- MPU6050 knock detection (single spike above 0.20g + 300ms quiet) must continue triggering FACE_WONDER
- MPU6050 tilt detection (>45° for >400ms) must continue triggering FACE_SCARED and holding until upright
- Screen tap detection via CST816S with 600ms debounce must continue working
- Head touch pad patting must continue triggering playful/affection reactions
- System boot initialization order and greeting must remain the same
- Sound clip mappings (GIDDY→RISA, CHEEKY→BAHHH, DERP→OI) must remain correct
- MPU6050 axis mapping (robotAccelX = -ay, robotAccelY = ax, robotAccelZ = az) must remain correct
- Servo gesture tables must remain zero-sum return-to-neutral
- Long-press chest touch during negative states must reset toward Loving/Smile

**Scope:**
All inputs that do NOT involve the 9 bug conditions should be completely unaffected by this fix. This includes:
- All motion detection logic (shake/knock/tilt thresholds and timing)
- Face rendering for all 37 non-CONFUSED expressions
- Sound synthesis and WAV clip playback
- Servo gesture step tables and timing
- Boot sequence and self-test mode

## Hypothesized Root Cause

Based on the bug description and code analysis, the root causes are:

1. **Touch Threshold Values (Bugs 1.1, 1.2)**: `Pins.h` defines `TOUCH_BODY_THRESH 1325` instead of 1350, and `TOUCH_HEAD_THRESH 1000` instead of a value below 1000 (e.g., 950). The comments even note "light finger ~1325" showing the threshold sits AT the measured value rather than above it for margin.

2. **Missing Gesture Serial Debug (Bug 1.3)**: `EilikServos.cpp` has no Serial.printf calls in `gesturePlayFace()` or `gestureUpdate()`. The `logEmotion()` function in the .ino only prints face name, not servo step details.

3. **Hard Wake Jump (Bug 1.4)**: `wakeIfAsleep()` calls `react(FACE_WONDER, 2500)` directly — a single hard transition instead of a multi-stage chain through Yawning → Sleepy → Smile.

4. **Missing BORED Stage in Idle Decay (Bug 1.5)**: The idle logic in `loop()` only has two stages: `idle > 45000` → yawn, `idle > 90000` → sleep. There is no intermediate Bored → Tired chain as specified in the datasheet.

5. **Wrong Anger Escalation Order (Bug 1.6)**: The screen poke handler uses `pokeCount == 6 → FUMING, pokeCount >= 7 → FURIOUS`, but the datasheet specifies Grumpy → Furious → Fuming (fuming is peak, not furious).

6. **Wrong Body Touch Mapping (Bug 1.7)**: The `TOUCH_BODY` handler always calls `react(FACE_CHEEKY)`, but the datasheet specifies chest touch triggers affection emotions (Giddy, Charmed, Loving). CHEEKY should only be triggered by a quick face/screen tap.

7. **Unbounded Float Accumulation (Bug 1.8)**: In `EilikFace.cpp`, the `FACE_CONFUSED` case uses `static float rot` that only increments, never wraps — will lose precision after extended display.

8. **I2C Bus Contention (Bug 1.9)**: Both CST816S and MPU6050 share Wire on GPIO 21/22. `motionUpdate()` runs every 30ms with no coordination relative to CST816S reads. The fix is to rate-limit or sequence MPU reads to avoid overlapping transactions.

## Correctness Properties

Property 1: Bug Condition - Touch Thresholds Match Requirements.md

_For any_ touch sensor reading where the body pad reads below 1350 or the head pad reads below 950, the fixed `touchPinsUpdate()` function SHALL detect the touch event and return the appropriate `TouchEvent`, matching the Requirements.md specified thresholds.

**Validates: Requirements 2.1, 2.2**

Property 2: Bug Condition - Gesture Serial Debug Output

_For any_ call to `gesturePlayFace(f)` where `f` has a non-null gesture table, the fixed function SHALL print via Serial the gesture name, step count, and for each step transition: step number, arm-left/arm-right/torso speeds, and duration in ms.

**Validates: Requirements 2.3**

Property 3: Bug Condition - Wake Chain From Sleep

_For any_ touch or motion event received while the robot is in the SLEEPING state, the fixed wake logic SHALL transition through Yawning → Sleepy → Smile (active state) rather than jumping directly to FACE_WONDER.

**Validates: Requirements 2.4**

Property 4: Bug Condition - Complete Idle Decay Chain

_For any_ period of inactivity, the fixed idle logic SHALL follow the full datasheet chain: active → Bored (after moderate idle) → Tired → Yawning → Sleeping, with distinct timing thresholds for each stage.

**Validates: Requirements 2.5**

Property 5: Bug Condition - Correct Anger Escalation Order

_For any_ sequence of repeated screen pokes, the fixed poke handler SHALL escalate through annoyed → grumpy → furious → fuming, where fuming is the peak anger state (the final escalation).

**Validates: Requirements 2.6**

Property 6: Bug Condition - Body Touch Triggers Affection

_For any_ body/chest touch event, the fixed handler SHALL trigger affection emotions (giddy, charmed, or loving) based on context, and SHALL NOT trigger FACE_CHEEKY. CHEEKY SHALL only be triggered by a quick face/screen tap.

**Validates: Requirements 2.7**

Property 7: Bug Condition - Confused Rotation Wraps

_For any_ duration that FACE_CONFUSED is active, the fixed renderer SHALL keep the `rot` accumulator within a safe floating-point range by wrapping at TWO_PI, preventing unbounded growth and visual artifacts.

**Validates: Requirements 2.8**

Property 8: Bug Condition - I2C Bus Coordination

_For any_ loop iteration where both CST816S and MPU6050 need I2C access, the fixed code SHALL rate-limit or sequence MPU6050 reads to avoid bus contention with the CST816S touch controller.

**Validates: Requirements 2.9**

Property 9: Preservation - Unchanged Subsystem Behavior

_For any_ input where none of the 9 bug conditions apply (normal face rendering, sound playback, IMU detection logic, servo gesture tables, boot sequence), the fixed code SHALL produce exactly the same behavior as the original code, preserving all existing functionality.

**Validates: Requirements 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9, 3.10, 3.11, 3.12**

## Fix Implementation

### Changes Required

Assuming our root cause analysis is correct:

**File**: `Eilik_Bot/Pins.h`

**Changes**:
1. **Fix body touch threshold**: Change `#define TOUCH_BODY_THRESH 1325` to `#define TOUCH_BODY_THRESH 1350`
2. **Fix head touch threshold**: Change `#define TOUCH_HEAD_THRESH 1000` to `#define TOUCH_HEAD_THRESH 950`

---

**File**: `Eilik_Bot/EilikServos.cpp`

**Function**: `gesturePlayFace()`, `gestureUpdate()`

**Changes**:
3. **Add gesture start debug**: In `gesturePlayFace()`, add Serial.printf printing emotion index, gesture name, and total steps
4. **Add step transition debug**: In `gestureUpdate()`, add Serial.printf when advancing to a new step showing step number, armL/armR/torso speeds, and duration
5. **Add gesture completion debug**: In `gestureUpdate()`, add Serial.printf when gesture finishes with total elapsed time

---

**File**: `Eilik_Bot/Eilik_Bot.ino`

**Function**: `wakeIfAsleep()`

**Changes**:
6. **Implement wake chain**: Replace `react(FACE_WONDER, 2500)` with a staged wake: Yawning (2s hold) → Sleepy (2s hold) → Smile. Use a state variable or timed sequence to drive the multi-step wake.

**Function**: Idle decay logic in `loop()`

**Changes**:
7. **Add Bored stage**: Insert a Bored state at ~30s idle, Tired at ~50s, Yawning at ~70s, Sleeping at ~90s (tune thresholds to fit the datasheet intent of a graduated chain)

**Function**: Screen poke handler in `loop()`

**Changes**:
8. **Fix anger escalation order**: Change the poke escalation from `grumpy → fuming → furious` to `grumpy → furious → fuming` — swap pokeCount == 6 to FURIOUS and pokeCount >= 7 to FUMING

**Function**: `TOUCH_BODY` handler in `loop()`

**Changes**:
9. **Fix body touch mapping**: Replace `react(FACE_CHEEKY)` with affection-based reactions (giddy, charmed, loving) similar to how head pats work, varying by touch duration or repetition context

---

**File**: `Eilik_Bot/EilikFace.cpp`

**Function**: `draw()` — `FACE_CONFUSED` case

**Changes**:
10. **Wrap rotation accumulator**: After `rot += ...`, add `if (rot > TWO_PI) rot -= TWO_PI;` to prevent unbounded growth

---

**File**: `Eilik_Bot/EilikMotion.cpp`

**Function**: `motionUpdate()`

**Changes**:
11. **Rate-limit I2C reads**: Increase the minimum read interval or add a guard that skips MPU reads when the CST816S may be mid-transaction (e.g., increase lastRead interval from 30ms to 50ms, or check a shared flag)

## Testing Strategy

### Validation Approach

The testing strategy follows a two-phase approach: first, surface counterexamples that demonstrate the bugs on unfixed code, then verify the fix works correctly and preserves existing behavior. Since this is embedded firmware (ESP32 Arduino), testing will combine serial-monitor observation, logic analysis, and structured test scenarios on hardware.

### Exploratory Bug Condition Checking

**Goal**: Surface counterexamples that demonstrate the bugs BEFORE implementing the fix. Confirm or refute the root cause analysis. If we refute, we will need to re-hypothesize.

**Test Plan**: Analyze the code statically to confirm threshold values, trace state machine paths, and identify the exact lines producing incorrect behavior. For runtime confirmation, use serial output to observe state transitions on the actual hardware.

**Test Cases**:
1. **Body Touch Threshold Test**: Read PIN_TOUCH_BODY with a light finger touch — readings between 1325 and 1350 will be missed (will fail on unfixed code)
2. **Wake Chain Test**: Put robot to sleep (wait 90s), then tap head — observe immediate FACE_WONDER without Yawning/Sleepy stages (will fail on unfixed code)
3. **Idle Decay Test**: Leave robot idle and observe via serial — no BORED state printed before YAWNING (will fail on unfixed code)
4. **Anger Order Test**: Poke screen 7 times rapidly — observe FUMING appears before FURIOUS at pokeCount 6 (will fail on unfixed code)
5. **Body Touch Mapping Test**: Touch body pad — observe CHEEKY always triggered, never affection emotions (will fail on unfixed code)
6. **Gesture Debug Test**: Trigger any emotion and check serial — no gesture step info printed (will fail on unfixed code)

**Expected Counterexamples**:
- Touch readings between 1325-1350 on body pad are not detected
- Wake produces immediate WONDER instead of Yawning→Sleepy chain
- Idle decay skips Bored and jumps from fidgets to Yawning
- Anger chain shows fuming at step 6, furious at step 7+ (reversed from spec)
- Body touch always produces CHEEKY regardless of context

### Fix Checking

**Goal**: Verify that for all inputs where the bug condition holds, the fixed function produces the expected behavior.

**Pseudocode:**
```
FOR ALL input WHERE isBugCondition(input) DO
  result := fixedFirmware(input)
  ASSERT expectedBehavior(result)
  -- Touch thresholds: body at 1350, head at 950
  -- Wake chain: Sleeping → Yawning → Sleepy → Smile
  -- Idle decay: active → Bored → Tired → Yawning → Sleeping
  -- Anger: annoyed → grumpy → furious → fuming
  -- Body touch: affection emotions, not CHEEKY
  -- Confused rot: always within [0, TWO_PI]
  -- Gesture debug: serial output present with step details
  -- I2C: no overlapping bus transactions
END FOR
```

### Preservation Checking

**Goal**: Verify that for all inputs where the bug condition does NOT hold, the fixed function produces the same result as the original function.

**Pseudocode:**
```
FOR ALL input WHERE NOT isBugCondition(input) DO
  ASSERT originalFirmware(input) = fixedFirmware(input)
  -- Face rendering: all 38 animations identical at ~40fps
  -- Sound: all synth effects and WAV clips play correctly
  -- IMU: shake/knock/tilt detection unchanged
  -- Servos: gesture tables and timing unchanged
  -- Boot: initialization order and greeting unchanged
END FOR
```

**Testing Approach**: Property-based testing is recommended for preservation checking because:
- It generates many test cases automatically across the input domain
- It catches edge cases that manual unit tests might miss
- It provides strong guarantees that behavior is unchanged for all non-buggy inputs

**Test Plan**: Observe behavior on UNFIXED code first for IMU events, screen taps below poke thresholds, and head pats, then write property-based tests capturing that behavior.

**Test Cases**:
1. **Face Rendering Preservation**: Verify all 38 face expressions render without visual changes (compare frame output logic)
2. **Sound Preservation**: Verify all sound clip mappings remain correct per Requirements.md
3. **IMU Detection Preservation**: Verify shake (3+ spikes/800ms), knock (single spike + quiet), and tilt (>45°/400ms) thresholds unchanged
4. **Servo Gesture Preservation**: Verify gesture step tables remain zero-sum and timing unchanged
5. **Head Pat Preservation**: Verify head touch still triggers giddy/charmed/loving/embarrassed chain

### Unit Tests

- Test touch threshold comparisons with boundary values (1349, 1350, 1351 for body; 949, 950, 951 for head)
- Test anger escalation sequence with incremental poke counts
- Test idle decay state transitions at each timing boundary
- Test wake chain state progression from SLEEPING
- Test body touch handler produces affection emotions
- Test `rot` variable stays bounded after many FACE_CONFUSED frames
- Test gesture debug serial output contains expected fields

### Property-Based Tests

- Generate random touch readings across the full ADC range and verify detection matches threshold specification
- Generate random poke count sequences and verify anger escalation follows correct order
- Generate random idle durations and verify correct decay state at each time point
- Generate random loop iterations with FACE_CONFUSED active and verify rot stays in [0, TWO_PI]
- Generate random inputs that are NOT bug conditions and verify behavior matches original

### Integration Tests

- Full boot → idle → sleep → wake cycle verifying correct chain via serial log
- Full screen poke escalation from wink through fuming verifying order via serial log
- Body touch → affection reaction → repeated → varied affection via serial log
- Mixed inputs (touch + motion) verifying no I2C bus errors via serial log
- Extended FACE_CONFUSED display (5+ minutes) verifying no visual artifacts
