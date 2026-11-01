// ============================================================
// Eilik_Master_Core — final integrated firmware (CORE trigger model)
//   face:   38 animated expressions (EilikFace)
//   voice:  38 face-matched sounds + WAV clips (EilikSound)
//   body:   3x MG90S 360 continuous servos, calibrated + non-blocking
//           (EilikServos — drift-corrected, smoothed)
//   senses: CST816S screen tap (face), 2x ESP32 touch pads (head/body),
//           MPU6050 in the robot frame (EilikMotion, EilikTouch)
//
// Behavior per Eilik_Emotion_Movement_Datasheet.md + Requirements.md.
// CORE model: each sensor -> its primary datasheet emotion, plus the sad /
// anger / positive escalation chains and the idle->sleep / wake chains.
// The datasheet's fancy combos (held-combo Ecstatic, long-press Loving, etc.)
// are approximated here — see Eilik_Master_Combo for the full-combo version.
//
// Serial @115200: prints "emotion N/38 NAME" and the body movement (EilikServos).
// Non-blocking throughout (Requirements.md embedded rules): faceUpdate,
// gestureUpdate, and sound task never block the loop.
// Wiring: Pins.h + Eilik_Bot/README.md.
// ============================================================
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <CST816S.h>
#include <Wire.h>
#include "Pins.h"
#include "EilikFace.h"
#include "EilikSound.h"
#include "EilikServos.h"
#include "EilikMotion.h"
#include "EilikTouch.h"

CST816S screenTouch(PIN_I2C_SDA, PIN_I2C_SCL, PIN_CTP_RST, PIN_CTP_INT);
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite buffer = TFT_eSprite(&tft);

// FaceExpr order (EilikFace.h) — for the "emotion N/38 NAME" serial line.
static const char* const FACE_NAMES[FACE_COUNT] = {
  "SMILE","GIDDY","ECSTATIC","CHARMED","SPARKLING JOY","PLAYFUL WINK","STARRY EYES",
  "CHEERFUL LAUGH","EAGER","ANTICIPATION","POUT","TEARY EYES","CRYING SOFTLY",
  "DISAPPOINTED","HEARTBROKEN","GRUMPY","FURIOUS","FUMING","ANNOYED","FLUSHED",
  "QUESTIONING","INVESTIGATING","THOUGHTFUL","CONFUSED SPIRAL","WONDER","PLAYFUL",
  "MISCHIEVOUS","CHEEKY TONGUE","SILLY DERP","LOVING","TIRED","SLEEPING ZZZ",
  "YAWNING","SLEEPY","SCARED","BORED MEH","EMBARRASSED","HUNGRY"
};

static void logEmotion(FaceExpr f) {
  Serial.printf("emotion %d/%d %s\n", f + 1, FACE_COUNT, FACE_NAMES[f]);
}

// ---- one reaction: face + matched sound + matched body movement, then hold ----
static unsigned long moodUntil = 0;
static bool holdMood = false;          // scared-while-tilted: don't auto-clear
static void react(FaceExpr f, uint16_t holdMs = 3000) {
  faceSet(f);
  logEmotion(f);
  soundPlayFace(f);
  gesturePlayFace(f);
  moodUntil = millis() + holdMs;
  holdMood = false;
}

// ---- interaction + idle bookkeeping ----
static unsigned long lastInteraction = 0, lastScreenTap = 0, nextFidget = 0;
static unsigned long lastPoke = 0, lastPat = 0, lastBody = 0, lastShake = 0, lastTilt = 0;
static uint8_t pokeCount = 0, patCount = 0, bodyCount = 0, shakeCount = 0, tiltCount = 0;
static uint8_t idleStage = 0;          // 0=active 1=bored 2=tired 3=yawning
static bool asleep = false;

// Idle timeouts (ms). Datasheet says minutes; kept short for bench testing —
// tune up for the finished robot.
#define T_BORED   30000UL
#define T_TIRED   50000UL
#define T_YAWN    70000UL
#define T_SLEEP   90000UL

static void markAwake() {
  lastInteraction = millis();
  idleStage = 0;
  nextFidget = millis() + random(18000, 40000);
}

// ---- wake chain: Sleeping -> Yawning -> Sleepy -> active (datasheet §4) ----
static uint8_t wakeStage = 0;          // 0=not waking, 1=yawning, 2=sleepy
static unsigned long wakeStageStart = 0;
#define WAKE_STAGE_MS 2000

static bool wakeIfAsleep() {
  if (!asleep) return false;
  asleep = false;
  wakeStage = 1;
  wakeStageStart = millis();
  react(FACE_YAWNING, WAKE_STAGE_MS);
  markAwake();
  return true;
}

static bool wakeChainUpdate() {
  if (wakeStage == 0) return false;
  unsigned long e = millis() - wakeStageStart;
  if (wakeStage == 1 && e >= WAKE_STAGE_MS) {
    wakeStage = 2; wakeStageStart = millis();
    react(FACE_SLEEPY, WAKE_STAGE_MS);
  } else if (wakeStage == 2 && e >= WAKE_STAGE_MS) {
    wakeStage = 0;
    react(FACE_SMILE, 3000);
  }
  return (wakeStage != 0);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nEilik_Master_Core boot");

  screenTouch.begin();                 // brings up Wire on 21/22 (shared bus)
  if (motionBegin()) Serial.println("MPU6050 ok (robot-frame axes)");
  else Serial.printf("MPU6050 not found (SDA=%d SCL=%d) - motion off\n", PIN_MPU_SDA, PIN_MPU_SCL);

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  buffer.setColorDepth(8);
  buffer.createSprite(240, 240);
  faceBegin(&buffer);

  servosBegin();
  soundBegin();

  faceSet(FACE_SMILE);
  logEmotion(FACE_SMILE);
  soundPlayBoot();
  gesturePlayFace(FACE_ECSTATIC);      // boot cheer
  markAwake();
}

void loop() {
  unsigned long now = millis();
  faceUpdate();
  gestureUpdate();

  if (wakeChainUpdate()) return;       // wake in progress: block new triggers

  // ---- gather sensor events ----
  TouchEvent te = touchPinsUpdate();
  MotionEvent me = motionUpdate();
  bool screenTap = false;
  if (screenTouch.available() && now - lastScreenTap > 600) { lastScreenTap = now; screenTap = true; }

  bool anyEvent = (te != TOUCH_NONE) || screenTap ||
                  me == MOTION_KNOCK || me == MOTION_SHAKE || me == MOTION_TILT;
  if (anyEvent) {
    if (wakeIfAsleep()) return;        // any touch/motion wakes first
    markAwake();
  }

  // ---- reactions, datasheet priority: motion(scary) > touch (§4) ----
  if (me == MOTION_TILT) {                         // being tipped over -> Scared
    if (now - lastTilt > 8000) tiltCount = 0;
    lastTilt = now; tiltCount++;
    if (tiltCount >= 2) react(FACE_ANNOYED, 4000); // repeated tip -> Annoyed (§1)
    else { react(FACE_SCARED, 60000); holdMood = true; }  // hold until put down
  } else if (me == MOTION_UPRIGHT && holdMood) {
    react(FACE_GIDDY);                              // relief when set back level
  } else if (me == MOTION_SHAKE) {                  // shake -> Confused, escalating
    if (now - lastShake > 12000) shakeCount = 0;
    lastShake = now; shakeCount++;
    if (shakeCount == 1)      react(FACE_CONFUSED, 3500);
    else if (shakeCount == 2) react(FACE_SCARED, 4000);
    else                      react(FACE_CRYING, 5000);   // enough!
  } else if (me == MOTION_KNOCK) {                  // single bump -> Wonder
    react(FACE_WONDER, 2500);
  } else if (te == TOUCH_BODY) {                    // chest = pet/hug -> affection
    if (now - lastBody > 10000) bodyCount = 0;
    lastBody = now; bodyCount++;
    if (bodyCount == 1)      react(FACE_GIDDY);
    else if (bodyCount <= 3) react(FACE_CHARMED, 3500);
    else                     react(FACE_LOVING, 4000);    // sustained -> Loving
  } else if (te == TOUCH_HEAD_PAT) {                // head pat -> playful/silly
    if (now - lastPat > 10000) patCount = 0;
    lastPat = now; patCount++;
    if (patCount >= 4) react(FACE_EMBARRASSED, 3500);     // over-patted
    else switch (random(3)) {
      case 0: react(FACE_WINK); break;
      case 1: react(FACE_PLAYFUL, 2500); break;
      case 2: react(FACE_MISCHIEVOUS, 4000); break;
    }
  } else if (screenTap) {                           // face poke -> annoyance chain
    if (now - lastPoke > 8000) pokeCount = 0;
    lastPoke = now; pokeCount++;
    if (pokeCount == 1)      react(FACE_CHEEKY, 2500);
    else if (pokeCount == 2) react(FACE_QUESTIONING, 2500);
    else if (pokeCount <= 4) react(FACE_ANNOYED, 6000);
    else if (pokeCount == 5) react(FACE_GRUMPY, 3500);
    else if (pokeCount == 6) react(FACE_FURIOUS, 3500);
    else                     react(FACE_FUMING, 4000);
  }

  // ---- mood decay back to a resting face ----
  if (!asleep && !holdMood && moodUntil && now > moodUntil) {
    moodUntil = 0;
    unsigned long idle = now - lastInteraction;
    FaceExpr rest = (idle > T_YAWN) ? FACE_YAWNING : (idle > T_TIRED) ? FACE_TIRED
                  : (idle > T_BORED) ? FACE_BORED : FACE_SMILE;
    faceSet(rest); logEmotion(rest); soundPlayFace(rest);
    if (!gestureBusy()) gesturePlayFace(rest);
  }

  // ---- idle life: fidget -> bored -> tired -> yawn -> sleep ----
  if (!asleep) {
    unsigned long idle = now - lastInteraction;
    if (idle > T_SLEEP) {
      asleep = true;
      faceSet(FACE_SLEEPING); logEmotion(FACE_SLEEPING); soundPlayFace(FACE_SLEEPING);
      servosSleep();                   // datasheet 32/38: torque off, limp
    } else if (idle > T_YAWN && idleStage < 3) { idleStage = 3; react(FACE_YAWNING, 4000); }
    else if (idle > T_TIRED && idleStage < 2) { idleStage = 2; react(FACE_TIRED, 4000); }
    else if (idle > T_BORED && idleStage < 1) { idleStage = 1; react(FACE_BORED, 8000); }
    else if (idle < T_BORED && now > nextFidget && !gestureBusy()) {
      nextFidget = now + random(18000, 40000);
      switch (random(10)) {            // entertains itself (datasheet fidgets)
        case 0: react(FACE_QUESTIONING, 2500); break;
        case 1: react(FACE_STARRY, 3000); break;
        case 2: react(FACE_PLAYFUL, 2500); break;
        case 3: react(FACE_THOUGHTFUL, 3000); break;
        case 4: react(FACE_INVESTIGATING, 3500); break;
        case 5: react(FACE_DERP, 3000); break;
        case 6: react(FACE_SPARKLE, 3000); break;
        case 7: react(FACE_HUNGRY, 4000); break;
        case 8: react(FACE_ANTICIPATION, 3000); break;
        case 9: react(FACE_POUT, 3000); break;
      }
    }
  }
}
