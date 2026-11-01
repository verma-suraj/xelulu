# Implementation Plan

## Overview

This plan fixes 9 logic bugs in the Eilik_Bot firmware where the state machine behavior diverges from the datasheet specification. The approach uses the bug condition methodology: first write exploration tests to confirm bugs exist, then write preservation tests to capture correct behavior, then implement the fix, and finally validate everything passes.

## Tasks

- [x] 1. Write bug condition exploration test
  - **Property 1: Bug Condition** - Eilik Bot Logic Divergence From Datasheet
  - **CRITICAL**: This test MUST FAIL on unfixed code - failure confirms the bugs exist
  - **DO NOT attempt to fix the test or the code when it fails**
  - **NOTE**: This test encodes the expected behavior - it will validate the fix when it passes after implementation
  - **GOAL**: Surface counterexamples that demonstrate the 9 logic bugs exist in the firmware
  - **Scoped PBT Approach**: For each deterministic bug, scope the property to the concrete failing case(s):
    - Bug 1.1: Body touch readings between 1325 and 1350 are NOT detected (threshold too low)
    - Bug 1.2: Head touch readings between 950 and 1000 are NOT detected (threshold too high)
    - Bug 1.3: gesturePlayFace() produces no Serial.printf output for gesture name, steps, or timing
    - Bug 1.4: Sleeping state + wake trigger → immediate FACE_WONDER instead of Yawning → Sleepy → Smile
    - Bug 1.5: Idle decay jumps from fidgets → yawn at 45s → sleep at 90s, never shows FACE_BORED
    - Bug 1.6: pokeCount == 6 → FUMING, pokeCount >= 7 → FURIOUS (reversed from spec: furious then fuming)
    - Bug 1.7: TOUCH_BODY always maps to FACE_CHEEKY instead of affection emotions (giddy/charmed/loving)
    - Bug 1.8: FACE_CONFUSED `rot` variable grows unbounded (no wrapping at TWO_PI)
    - Bug 1.9: No I2C rate-limiting or sequencing between CST816S and MPU6050 reads
  - Test assertions should match Expected Behavior Properties from design:
    - Body threshold == 1350, head threshold == 950
    - Wake chain: Sleeping → Yawning → Sleepy → Smile
    - Idle chain: active → Bored → Tired → Yawning → Sleeping
    - Anger chain: annoyed → grumpy → furious → fuming
    - Body touch → affection emotions, NOT CHEEKY
    - rot stays within [0, TWO_PI]
    - Gesture serial debug output present
    - I2C reads are rate-limited/sequenced
  - Run test on UNFIXED code
  - **EXPECTED OUTCOME**: Test FAILS (this is correct - it proves the bugs exist)
  - Document counterexamples found:
    - e.g., "touchPinsUpdate() with body reading 1340 returns NO_TOUCH instead of TOUCH_BODY"
    - e.g., "wakeIfAsleep() calls react(FACE_WONDER) directly, no Yawning/Sleepy stages"
    - e.g., "pokeCount 6 triggers FUMING but spec says FURIOUS at step 6"
  - Mark task complete when test is written, run, and failure is documented
  - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9_

- [x] 2. Write preservation property tests (BEFORE implementing fix)
  - **Property 2: Preservation** - Unchanged Subsystem Behavior
  - **IMPORTANT**: Follow observation-first methodology
  - **Observe behavior on UNFIXED code for non-buggy inputs:**
    - Observe: All 38 face expressions render correctly via TFT_eSprite at ~40fps with breathing bob and organic blinks
    - Observe: I2S audio synthesis plays all 38 emotion sounds and WAV clips non-blockingly on Core 0
    - Observe: MPU6050 shake detection (3+ spikes/800ms above 0.55g) triggers confused/furious/crying escalation
    - Observe: MPU6050 knock detection (single spike >0.20g + 300ms quiet) triggers FACE_WONDER
    - Observe: MPU6050 tilt detection (>45° for >400ms) triggers FACE_SCARED and holds until upright
    - Observe: Screen tap via CST816S with 600ms debounce triggers face-poke reactions
    - Observe: Head touch patting triggers giddy/charmed/loving/embarrassed chain
    - Observe: Boot initializes display, CST816S, MPU6050, I2S, servos in order and plays greeting
    - Observe: Sound clip mappings: GIDDY→RISA, CHEEKY→BAHHH, DERP→OI all correct
    - Observe: MPU6050 axis mapping: robotAccelX = -ay, robotAccelY = ax, robotAccelZ = az
    - Observe: Servo gesture tables are zero-sum return-to-neutral
    - Observe: Long-press chest touch during negative states resets toward Loving/Smile
  - **Write property-based tests capturing observed behavior patterns:**
    - Property: For all touch readings > TOUCH_HEAD_THRESH on head pad, head pat reaction fires correctly
    - Property: For all IMU acceleration sequences matching shake/knock/tilt patterns, correct emotion triggers
    - Property: For all face expression indices 0-37 (excluding CONFUSED rot behavior), rendering logic unchanged
    - Property: For all servo gesture tables, step values sum to zero (return-to-neutral preserved)
    - Property: For all sound clip mappings, clip assignment matches Requirements.md
    - Property: Boot sequence order is preserved (display → touch → IMU → audio → servos → greeting)
    - Property: For screen poke counts 1-4, reactions are wink/cheeky (not anger) — below anger threshold
    - Property: Long-press body touch during negative state triggers comfort reset
  - Run tests on UNFIXED code
  - **EXPECTED OUTCOME**: Tests PASS (this confirms baseline behavior to preserve)
  - Mark task complete when tests are written, run, and passing on unfixed code
  - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9, 3.10, 3.11, 3.12_

- [x] 3. Fix for Eilik Bot 9 logic bugs diverging from datasheet specification

  - [x] 3.1 Fix touch thresholds in Pins.h
    - Change `#define TOUCH_BODY_THRESH 1325` to `#define TOUCH_BODY_THRESH 1350`
    - Change `#define TOUCH_HEAD_THRESH 1000` to `#define TOUCH_HEAD_THRESH 950`
    - _Bug_Condition: isBugCondition(input) where input.event == TOUCH_BODY AND threshold_used == 1325, OR input.event == TOUCH_HEAD AND threshold_used == 1000_
    - _Expected_Behavior: Body touch detected at 1350, head touch detected at 950 per Requirements.md_
    - _Preservation: Touch detection logic structure unchanged, only threshold constants modified_
    - _Requirements: 2.1, 2.2_

  - [x] 3.2 Add serial debug output for gestures in EilikServos.cpp
    - In `gesturePlayFace()`: Add Serial.printf printing emotion index, gesture name, and total steps at gesture start
    - In `gestureUpdate()`: Add Serial.printf when advancing to a new step showing step number, armL/armR/torso speeds and directions, and duration in ms
    - In `gestureUpdate()`: Add Serial.printf when gesture completes showing total elapsed time
    - _Bug_Condition: isBugCondition(input) where input.event == GESTURE_START AND no_serial_output_
    - _Expected_Behavior: Serial output includes gesture name, step count, per-step speeds/directions/duration, and completion time_
    - _Preservation: Servo gesture timing and step execution logic unchanged, only adding print statements_
    - _Requirements: 2.3_

  - [x] 3.3 Implement wake chain in Eilik_Bot.ino
    - Replace `react(FACE_WONDER, 2500)` in `wakeIfAsleep()` with staged wake: Yawning (2s hold) → Sleepy (2s hold) → Smile (active state)
    - Use a state variable or timed sequence to drive the multi-step wake transition
    - Ensure wake chain completes before accepting new interactions
    - _Bug_Condition: isBugCondition(input) where input.state == SLEEPING AND any_wake_trigger AND next_state == FACE_WONDER_
    - _Expected_Behavior: Sleeping → Yawning → Sleepy → Smile per datasheet wake chain_
    - _Preservation: Sleep detection and entry logic unchanged, only wake-up transition modified_
    - _Requirements: 2.4_

  - [x] 3.4 Add BORED stage to idle decay chain in Eilik_Bot.ino
    - Insert Bored state at ~30s idle threshold
    - Set Tired at ~50s idle threshold
    - Adjust Yawning to ~70s idle threshold
    - Keep Sleeping at ~90s idle threshold
    - Full chain: active → Bored (30s) → Tired (50s) → Yawning (70s) → Sleeping (90s)
    - _Bug_Condition: isBugCondition(input) where input.idle_elapsed AND decay_skips_BORED_
    - _Expected_Behavior: Complete idle decay chain with distinct BORED stage before Tired_
    - _Preservation: Sleep entry behavior unchanged, only intermediate states added/adjusted_
    - _Requirements: 2.5_

  - [x] 3.5 Fix anger escalation order in Eilik_Bot.ino
    - Change pokeCount == 5 → ANNOYED (no change)
    - Change pokeCount == 6 → GRUMPY (no change if already correct)
    - Change pokeCount == 6 or 7 mapping: swap FURIOUS and FUMING so FURIOUS comes before FUMING
    - Correct order: wink → wink → annoyed → annoyed → grumpy → furious → fuming (peak)
    - _Bug_Condition: isBugCondition(input) where input.event == SCREEN_TAP AND poke_count >= 5 AND anger_order != [annoyed,grumpy,furious,fuming]_
    - _Expected_Behavior: Anger escalation follows annoyed → grumpy → furious → fuming with fuming as peak_
    - _Preservation: Screen tap detection mechanism and debounce timing unchanged_
    - _Requirements: 2.6_

  - [x] 3.6 Fix body touch mapping in Eilik_Bot.ino
    - Replace `react(FACE_CHEEKY)` in TOUCH_BODY handler with affection emotion logic
    - Implement context-based affection: giddy (first touch), charmed (repeated), loving (sustained/long-press)
    - Ensure CHEEKY is only triggered by quick face/screen tap, not body touch
    - _Bug_Condition: isBugCondition(input) where input.event == TOUCH_BODY AND reaction == FACE_CHEEKY_
    - _Expected_Behavior: Body touch triggers affection emotions (giddy/charmed/loving), CHEEKY only from screen tap_
    - _Preservation: Head touch pat chain and long-press comfort reset unchanged_
    - _Requirements: 2.7_

  - [x] 3.7 Wrap rotation accumulator in EilikFace.cpp
    - In the FACE_CONFUSED draw case, after `rot += ...` add wrapping: `if (rot > TWO_PI) rot -= TWO_PI;`
    - This prevents unbounded float growth and visual jitter after extended display
    - _Bug_Condition: isBugCondition(input) where input.face == FACE_CONFUSED AND rot_unbounded_
    - _Expected_Behavior: rot stays within [0, TWO_PI] range indefinitely, no precision loss_
    - _Preservation: FACE_CONFUSED visual appearance unchanged (wrapping is visually seamless for periodic rotation)_
    - _Requirements: 2.8_

  - [x] 3.8 Add I2C bus coordination in EilikMotion.cpp
    - Rate-limit MPU6050 reads to avoid contention with CST816S on shared I2C bus (GPIO 21/22)
    - Increase minimum read interval from 30ms to 50ms, or add a guard/flag to sequence reads
    - Ensure MPU6050 read never overlaps with a CST816S transaction
    - _Bug_Condition: isBugCondition(input) where input.event == I2C_ACCESS AND no_bus_coordination_
    - _Expected_Behavior: MPU6050 reads rate-limited/sequenced to avoid bus contention with CST816S_
    - _Preservation: IMU detection thresholds and timing windows unchanged, only read scheduling modified_
    - _Requirements: 2.9_

  - [x] 3.9 Verify bug condition exploration test now passes
    - **Property 1: Expected Behavior** - Eilik Bot Logic Matches Datasheet
    - **IMPORTANT**: Re-run the SAME test from task 1 - do NOT write a new test
    - The test from task 1 encodes the expected behavior for all 9 bug conditions
    - When this test passes, it confirms all expected behaviors are satisfied:
      - Body threshold 1350 detects light touches
      - Head threshold 950 detects touches with margin
      - Gesture serial debug output present
      - Wake chain: Sleeping → Yawning → Sleepy → Smile
      - Idle chain: active → Bored → Tired → Yawning → Sleeping
      - Anger chain: annoyed → grumpy → furious → fuming
      - Body touch → affection emotions
      - rot wraps at TWO_PI
      - I2C reads are rate-limited
    - Run bug condition exploration test from step 1
    - **EXPECTED OUTCOME**: Test PASSES (confirms all 9 bugs are fixed)
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.9_

  - [x] 3.10 Verify preservation tests still pass
    - **Property 2: Preservation** - Unchanged Subsystem Behavior
    - **IMPORTANT**: Re-run the SAME tests from task 2 - do NOT write new tests
    - Run preservation property tests from step 2
    - **EXPECTED OUTCOME**: Tests PASS (confirms no regressions)
    - Confirm all preservation tests still pass after fix:
      - Face rendering for all 38 expressions unchanged
      - Sound synthesis and clip mappings unchanged
      - IMU shake/knock/tilt detection unchanged
      - Servo gesture tables zero-sum and timing unchanged
      - Boot sequence and greeting unchanged
      - Head pat chain unchanged
      - Screen tap detection mechanism unchanged
      - Long-press comfort reset unchanged

- [x] 4. Checkpoint - Ensure all tests pass
  - Run full test suite (bug condition + preservation property tests)
  - Verify all 9 bug condition tests now pass (expected behavior satisfied)
  - Verify all preservation tests still pass (no regressions introduced)
  - Verify serial debug output shows correct gesture info, state transitions, and sensor events
  - Upload firmware to ESP32 and perform manual smoke test on hardware:
    - Light body touch detected (threshold 1350)
    - Head pat triggers giddy/charmed chain
    - Sleep → wake shows Yawning → Sleepy → Smile
    - Idle decay shows Bored stage before Tired
    - 7 screen pokes escalate through correct anger order ending at fuming
    - Body touch triggers affection, not CHEEKY
    - Extended FACE_CONFUSED shows no visual jitter
    - No I2C bus errors in serial log
  - Ensure all tests pass, ask the user if questions arise.


## Task Dependency Graph

```json
{
  "waves": [
    {
      "name": "Wave 1 - Exploration & Preservation Tests",
      "tasks": ["1", "2"],
      "description": "Write tests BEFORE implementing the fix to understand the bugs and capture baseline behavior"
    },
    {
      "name": "Wave 2 - Implementation",
      "tasks": ["3.1", "3.2", "3.3", "3.4", "3.5", "3.6", "3.7", "3.8"],
      "dependencies": ["1", "2"],
      "description": "Apply all 9 bug fixes across Pins.h, EilikServos.cpp, Eilik_Bot.ino, EilikFace.cpp, and EilikMotion.cpp"
    },
    {
      "name": "Wave 3 - Verification",
      "tasks": ["3.9", "3.10"],
      "dependencies": ["3.1", "3.2", "3.3", "3.4", "3.5", "3.6", "3.7", "3.8"],
      "description": "Re-run exploration and preservation tests to confirm fix works and no regressions"
    },
    {
      "name": "Wave 4 - Checkpoint",
      "tasks": ["4"],
      "dependencies": ["3.9", "3.10"],
      "description": "Final validation - all tests pass, hardware smoke test"
    }
  ]
}
```

## Notes

- This is an embedded firmware project (ESP32 Arduino) — property-based tests will use static code analysis and serial output verification rather than traditional unit test frameworks
- Tests 1 and 2 must be written and run BEFORE any implementation changes (tasks 3.x)
- The exploration test (task 1) is expected to FAIL on unfixed code — this confirms the bugs exist
- The preservation tests (task 2) are expected to PASS on unfixed code — this captures baseline behavior
- After implementation, task 3.9 re-runs the same exploration test expecting it to PASS
- After implementation, task 3.10 re-runs the same preservation tests expecting them to still PASS
- Files modified: Pins.h, EilikServos.cpp, Eilik_Bot.ino, EilikFace.cpp, EilikMotion.cpp
