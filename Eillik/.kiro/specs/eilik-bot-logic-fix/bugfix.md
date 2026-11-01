# Bugfix Requirements Document

## Introduction

The Eilik_Bot main firmware integrates face, sound, servo gestures, touch sensing, and IMU motion into one state machine. While individual subsystems work correctly in isolation (Face_Test, Emotion_Test), the integrated code has 9 logic bugs where the state machine behavior, thresholds, and escalation chains diverge from the Eilik_Emotion_Movement_Datasheet.md and Requirements.md specifications. These bugs cause incorrect emotional responses to touch, wrong anger/wake/idle chains, missing serial debug output for gestures, and a floating-point precision issue in the face renderer.

## Bug Analysis

### Current Behavior (Defect)

1.1 WHEN the body touch pad (GPIO14) is touched with a single finger THEN the system uses threshold 1325 instead of the specified 1350, causing some light touches to be missed

1.2 WHEN the head touch pad (GPIO32) is touched THEN the system uses threshold 1000 instead of a value below 1000 (e.g., 950), causing the threshold to sit exactly at the boundary rather than safely below it as specified in Requirements.md

1.3 WHEN a gesture is played via gesturePlayFace() THEN the system produces no Serial output identifying which gesture/movement is executing, no step-by-step servo speed/direction info, no timing data, and no indication of when a gesture starts or completes — making it impossible to verify that physical servo movement matches the datasheet timing and direction specifications as required by Requirements.md

1.4 WHEN the robot is in the SLEEPING state and any touch/motion is detected THEN the system transitions directly to FACE_WONDER (hard jump) instead of following the datasheet wake chain: Sleeping → Yawning → Sleepy → active state

1.5 WHEN the robot is idle and the idle decay timer elapses THEN the system skips the BORED stage and goes from active → fidgets → yawn at 45s → sleep at 90s, instead of the datasheet chain: active → Bored → Tired → Yawning → Sleeping

1.6 WHEN the face/screen is tapped repeatedly THEN the system escalates through wink → annoyed → grumpy → fuming → furious, but the datasheet anger chain specifies grumpy → furious → fuming (fuming is peak anger, not before furious)

1.7 WHEN the body/chest touch pad is touched THEN the system always triggers FACE_CHEEKY, but the datasheet specifies chest touch should trigger affection emotions (giddy, charmed, loving depending on context), while CHEEKY should be triggered by a quick face/screen tap

1.8 WHEN FACE_CONFUSED is active for extended periods THEN the static float `rot` variable in EilikFace.cpp accumulates unboundedly (rot += 0.02 + 0.11*...) and will eventually lose floating-point precision, causing visual artifacts in the spiral animation

1.9 WHEN both the CST816S touch controller and MPU6050 IMU perform I2C transactions on the shared bus (GPIO 21/22) every loop iteration THEN there is no coordination or mutual exclusion between the two, potentially causing intermittent read failures or garbled data on the shared I2C bus

### Expected Behavior (Correct)

2.1 WHEN the body touch pad (GPIO14) is touched with a single finger THEN the system SHALL use threshold 1350 as specified in Requirements.md so that light single-finger touches are reliably detected

2.2 WHEN the head touch pad (GPIO32) is touched THEN the system SHALL use a threshold below 1000 (e.g., 950) as specified in Requirements.md ("for the head is below 1000") to ensure reliable detection with margin

2.3 WHEN a gesture begins playing via gesturePlayFace() THEN the system SHALL print via Serial:
- Gesture start: emotion name, gesture index, total number of steps
- Each step transition: step number, arm-left speed and direction, arm-right speed and direction, torso speed and direction, step duration in ms
- Gesture completion: total elapsed time
- Touch sensor events: which sensor triggered (head/body/screen), raw reading value, threshold used, event type (tap/long-press/combo)
- IMU events: event type (shake/knock/tilt/upright), raw acceleration delta or angle
- State transitions: previous state → new state, reason for transition
- Idle timer status: current idle duration, next decay threshold
This enables the user to verify that physical servo directions and timing match the datasheet, sensor events fire correctly, and state transitions are logically correct

2.4 WHEN the robot is in the SLEEPING state and any touch/motion is detected THEN the system SHALL follow the datasheet wake chain: transition to Yawning first, then to Sleepy, then to the normal active state (SMILE)

2.5 WHEN the robot is idle THEN the system SHALL follow the complete datasheet idle decay chain: active state → Bored (after moderate idle period) → Tired (after longer idle) → Yawning (transitional) → Sleeping (deepest idle), with distinct timing thresholds for each stage

2.6 WHEN the face/screen is tapped repeatedly THEN the system SHALL escalate through the correct datasheet anger chain: annoyed → grumpy → furious → fuming (fuming is peak anger, the final escalation stage)

2.7 WHEN the body/chest touch pad is touched THEN the system SHALL trigger affection emotions (giddy, charmed, or loving depending on touch duration/repetition) per the datasheet, and CHEEKY SHALL only be triggered by a quick face/screen tap

2.8 WHEN FACE_CONFUSED is active THEN the system SHALL wrap or reset the rotation accumulator variable to prevent unbounded growth, keeping it within a safe floating-point range (e.g., wrapping at TWO_PI or using fmodf)

2.9 WHEN both the CST816S and MPU6050 need to access the shared I2C bus THEN the system SHALL ensure the MPU6050 reads are rate-limited or sequenced to avoid bus contention with the CST816S touch controller reads

### Unchanged Behavior (Regression Prevention)

3.1 WHEN the display is connected and rendering THEN the system SHALL CONTINUE TO render all 38 face expressions with smooth animations at ~40fps using the TFT_eSprite-based EilikFace renderer

3.2 WHEN the I2S audio system is running THEN the system SHALL CONTINUE TO synthesize and play all 38 emotion sounds and WAV clips non-blockingly on Core 0 via the FreeRTOS sound task

3.3 WHEN the MPU6050 detects shake events (3+ spikes within 800ms above 0.55g) THEN the system SHALL CONTINUE TO trigger confused/furious/crying escalation reactions

3.4 WHEN the MPU6050 detects a knock event (single spike above 0.20g followed by 300ms quiet) THEN the system SHALL CONTINUE TO trigger FACE_WONDER

3.5 WHEN the MPU6050 detects sustained tilt (>45° for >400ms) THEN the system SHALL CONTINUE TO trigger FACE_SCARED and hold until upright

3.6 WHEN a screen tap is detected via CST816S with 600ms debounce THEN the system SHALL CONTINUE TO trigger face-poke reactions (the specific emotions change per bug 6, but the screen-tap detection mechanism itself is preserved)

3.7 WHEN the head touch pad is patted THEN the system SHALL CONTINUE TO trigger playful/affection reactions (giddy, charmed, loving, embarrassed on repeated pats) as currently implemented

3.8 WHEN the system boots THEN it SHALL CONTINUE TO initialize all subsystems (display, CST816S, MPU6050, I2S audio, servos) in the correct order and play the boot greeting

3.9 WHEN sound clip mappings are used THEN the system SHALL CONTINUE TO use the correct Requirements.md mappings: GIDDY→RISA, CHEEKY→BAHHH, DERP→OI and all other clip assignments

3.10 WHEN the MPU6050 reads accelerometer data THEN the system SHALL CONTINUE TO apply the correct axis mapping per mpu6050_orientation.md (robotAccelX = -ay, robotAccelY = ax, robotAccelZ = az)

3.11 WHEN servo gestures are constructed THEN the system SHALL CONTINUE TO use zero-sum return-to-neutral tables ensuring poses return to neutral by construction

3.12 WHEN a long-press chest/body touch occurs during negative emotional states (sad/anger chains) THEN the system SHALL reset toward Loving or Smile per the datasheet's "comfort reset" mechanism
