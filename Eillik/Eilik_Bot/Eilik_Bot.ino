// ============================================================
// Eilik_Bot — Eilik-style desk robot on ESP32
//   face:    38 animated expressions (EilikFace, from Animation sheet.png)
//   voice:   38 face-matched synthesized effects + WAV clips (EilikSound)
//   body:    3x MG90S 360 continuous servos — arms + torso, one movement
//            per emotion from Eilik_Emotion_Movement_Datasheet.md
//            (head servo NOT installed — no head axis anywhere)
//   senses:  CST816S screen touch, 2x ESP32 touch pads (absolute
//            thresholds, no boot calibration), MPU6050 in the robot
//            frame per mpu6050_orientation.md
//   debug:   Serial 115200 — every emotion prints "emotion N/38 NAME"
// Wiring: see Pins.h + README.md
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

#define SELFTEST 0   // 1 = at boot: cycle every face+sound, then each servo

// Idle decay timings (ms): active -> bored -> tired -> yawning -> sleeping.
// Longer than the old 30/50/70/90s so a quiet desk robot doesn't nod off
// constantly. Use the serial console (type 31/33/32...) to test these instantly.
#define IDLE_BORED_MS    45000UL
#define IDLE_TIRED_MS    90000UL
#define IDLE_YAWN_MS    135000UL
#define IDLE_SLEEP_MS   180000UL

CST816S screenTouch(PIN_I2C_SDA, PIN_I2C_SCL, PIN_CTP_RST, PIN_CTP_INT);
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite buffer = TFT_eSprite(&tft);

// must match the FaceExpr enum order in EilikFace.h (sheet order)
static const char* const FACE_NAMES[FACE_COUNT] = {
  "SMILE", "GIDDY", "ECSTATIC", "CHARMED", "SPARKLING JOY",
  "PLAYFUL WINK", "STARRY EYES", "CHEERFUL LAUGH", "EAGER", "ANTICIPATION",
  "POUT", "TEARY EYES", "CRYING SOFTLY", "DISAPPOINTED", "HEARTBROKEN",
  "GRUMPY", "FURIOUS", "FUMING", "ANNOYED", "FLUSHED",
  "QUESTIONING", "INVESTIGATING", "THOUGHTFUL", "CONFUSED SPIRAL", "WONDER",
  "PLAYFUL", "MISCHIEVOUS", "CHEEKY TONGUE", "SILLY DERP", "LOVING",
  "TIRED", "SLEEPING ZZZ", "YAWNING", "SLEEPY", "SCARED",
  "BORED MEH", "EMBARRASSED", "HUNGRY"
};

static void logEmotion(FaceExpr f) {
  Serial.printf("emotion %d/%d %s\n", f + 1, FACE_COUNT, FACE_NAMES[f]);
}

// Idle fidgets: the "entertains itself" pool. Includes the light emotions that
// have no dedicated touch trigger (LAUGH, EAGER, FLUSHED, CHEEKY, ECSTATIC) so
// every emotion is reachable without a combo state machine. BORED/TIRED/etc.
// are owned by the idle-decay stages, not here.
static const FaceExpr FIDGETS[] = {
  FACE_QUESTIONING, FACE_STARRY, FACE_PLAYFUL, FACE_THOUGHTFUL, FACE_MISCHIEVOUS,
  FACE_POUT, FACE_INVESTIGATING, FACE_DERP, FACE_SPARKLE, FACE_HUNGRY,
  FACE_ANTICIPATION, FACE_LAUGH, FACE_EAGER, FACE_FLUSHED, FACE_CHEEKY, FACE_ECSTATIC
};
static const uint8_t FIDGET_COUNT = sizeof(FIDGETS) / sizeof(FIDGETS[0]);

// ---- mood: face + its matched sound + its datasheet body movement,
//      holds, then decays back to the resting face ----
unsigned long moodUntil = 0;
bool holdMood = false;          // scared-while-held: don't auto-clear
void react(FaceExpr f, uint16_t holdMs = 3000) {
  faceSet(f);
  logEmotion(f);
  soundPlayFace(f);             // sound always matches the emotion
  gesturePlayFace(f);           // movement always matches the emotion
  moodUntil = millis() + holdMs;
  holdMood = false;
}

// ---- interaction bookkeeping ----
unsigned long lastInteraction = 0;
unsigned long lastScreenTap = 0;
uint8_t pokeCount = 0, patCount = 0, shakeCount = 0, bodyTouchCount = 0;
unsigned long lastPoke = 0, lastPat = 0, lastShake = 0, lastBodyTouch = 0;
bool asleep = false;
uint8_t idleStage = 0;  // 0=active, 1=bored, 2=tired, 3=yawning
unsigned long nextFidget = 0;

// ---- wake chain state machine ----
// Stages: 0 = not waking, 1 = Yawning, 2 = Sleepy, 3 = done (Smile)
uint8_t wakeStage = 0;
unsigned long wakeStageStart = 0;
#define WAKE_STAGE_DURATION 2000  // 2 seconds per stage

void markAwakeActivity() {
  lastInteraction = millis();
  idleStage = 0;
  nextFidget = millis() + random(18000, 40000);
}

bool wakeIfAsleep() {
  if (!asleep) return false;
  asleep = false;
  // Start the multi-stage wake chain: FACE_YAWNING → FACE_SLEEPY → Smile
  // Stage 1: Yawning (2s hold), Stage 2: Sleepy (2s hold), then active Smile
  wakeStage = 1;
  wakeStageStart = millis();
  faceSet(FACE_YAWNING);
  logEmotion(FACE_YAWNING);
  soundPlayFace(FACE_YAWNING);
  gesturePlayFace(FACE_YAWNING);
  // Next stages (FACE_SLEEPY then FACE_SMILE) driven by wakeChainUpdate()
  markAwakeActivity();
  return true;
}

// Drives the wake chain state machine. Returns true while wake is in progress
// (blocks new interactions until complete).
bool wakeChainUpdate() {
  if (wakeStage == 0) return false;  // not waking

  unsigned long now = millis();
  unsigned long elapsed = now - wakeStageStart;

  if (wakeStage == 1 && elapsed >= WAKE_STAGE_DURATION) {
    // Yawning complete → transition to Sleepy
    wakeStage = 2;
    wakeStageStart = now;
    faceSet(FACE_SLEEPY);
    logEmotion(FACE_SLEEPY);
    soundPlayFace(FACE_SLEEPY);
    gesturePlayFace(FACE_SLEEPY);
  } else if (wakeStage == 2 && elapsed >= WAKE_STAGE_DURATION) {
    // Sleepy complete → transition to Smile (active state), wake chain done
    wakeStage = 0;
    faceSet(FACE_SMILE);
    logEmotion(FACE_SMILE);
    soundPlayFace(FACE_SMILE);
    gesturePlayFace(FACE_SMILE);
    moodUntil = now + 3000;  // hold smile briefly before allowing decay
  }

  return (wakeStage != 0);  // still in progress
}

// ---- serial test console ----------------------------------------------
// Type a number 1..38 + Enter to play any emotion (face + sound + movement),
// so every gesture in the datasheet is testable on demand. Letters:
//   h/? = list emotions   t = touch pad raw values   w = wake   s = force sleep
void printEmotionList() {
  Serial.println(F("\n--- Emotions (type the number + Enter to play) ---"));
  for (uint8_t i = 0; i < FACE_COUNT; i++)
    Serial.printf("%2d %s%s", i + 1, FACE_NAMES[i], (i % 3 == 2) ? "\n" : "\t");
  Serial.println(F("\nletters: h=list  t=touch raw  w=wake  s=force sleep\n"));
}

static int serialNum = -1;   // -1 = no digits buffered yet
void handleSerial() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch >= '0' && ch <= '9') {
      serialNum = (serialNum < 0 ? 0 : serialNum) * 10 + (ch - '0');
      continue;
    }
    if (serialNum >= 1 && serialNum <= FACE_COUNT) {   // a number was entered
      asleep = false; wakeStage = 0; holdMood = false; // a command always wakes
      markAwakeActivity();
      Serial.printf("[Console] play %d/%d\n", serialNum, FACE_COUNT);
      react((FaceExpr)(serialNum - 1), 4000);
    } else if (ch == 't' || ch == 'T') {
      touchPinsDumpRaw();
    } else if (ch == 'h' || ch == 'H' || ch == '?' || ch == 'l' || ch == 'L') {
      printEmotionList();
    } else if (ch == 'w' || ch == 'W') {
      asleep = false; wakeStage = 0; holdMood = false;
      markAwakeActivity();
      react(FACE_SMILE);
    } else if (ch == 's' || ch == 'S') {
      lastInteraction = millis() - IDLE_SLEEP_MS - 1;  // trip the sleep timer
      Serial.println(F("[Console] forcing sleep"));
    }
    serialNum = -1;
  }
}

#if SELFTEST
void selfTest() {
  for (uint8_t f = 0; f < FACE_COUNT; f++) {
    faceSet((FaceExpr)f);
    logEmotion((FaceExpr)f);
    soundPlayFace((FaceExpr)f);
    unsigned long t = millis();
    while (millis() - t < 1800) { faceUpdate(); }
  }
  // three movements that together exercise every servo + direction
  FaceExpr gs[] = {FACE_ECSTATIC, FACE_INVESTIGATING, FACE_DERP};
  for (FaceExpr g : gs) {
    gesturePlayFace(g);
    while (gestureBusy()) { gestureUpdate(); servosUpdate(); faceUpdate(); }
    delay(300);
  }
  faceSet(FACE_SMILE);
}
#endif

void setup() {
  Serial.begin(115200);
  Serial.println("\nEilik_Bot boot");

  screenTouch.begin();               // brings up Wire on 21/22
  if (motionBegin())
    Serial.println("MPU6050 ok (robot-frame axes per mpu6050_orientation.md)");
  else
    Serial.printf("MPU6050 not found (SDA=%d SCL=%d) - motion senses off\n",
                  PIN_MPU_SDA, PIN_MPU_SCL);

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  buffer.setColorDepth(8);
  buffer.createSprite(240, 240);
  faceBegin(&buffer);
  faceSet(FACE_SMILE);
  logEmotion(FACE_SMILE);

  servosBegin();
  soundBegin();

  soundPlayBoot();
  gesturePlayFace(FACE_ECSTATIC);    // boot cheer
  markAwakeActivity();

#if SELFTEST
  selfTest();
#endif

  printEmotionList();   // serial console usage: type 1..38 + Enter to test any emotion
}

void loop() {
  unsigned long now = millis();
  faceUpdate();
  gestureUpdate();
  servosUpdate();                   // ramp servo speed (softens current spikes)
  handleSerial();                   // serial test console (non-blocking)

  // ---- wake chain in progress: block new interactions until complete ----
  if (wakeChainUpdate()) return;

  // ---- gather events ----
  TouchEvent te = touchPinsUpdate();
  MotionEvent me = motionUpdate();
  bool screenTap = false;
  if (screenTouch.available() && now - lastScreenTap > 600) {
    lastScreenTap = now;
    screenTap = true;
    Serial.println("TOUCH: FACE (screen)");   // print where the touch landed
  }
  if (me == MOTION_KNOCK)        Serial.println("IMU: KNOCK");
  else if (me == MOTION_SHAKE)   Serial.println("IMU: SHAKE");
  else if (me == MOTION_TILT)    Serial.println("IMU: TILT (picked up)");
  else if (me == MOTION_UPRIGHT) Serial.println("IMU: UPRIGHT (put down)");

  bool anyEvent = (te != TOUCH_NONE) || screenTap ||
                  (me == MOTION_KNOCK || me == MOTION_SHAKE || me == MOTION_TILT);
  if (anyEvent) {
    bool justWoke = wakeIfAsleep();
    markAwakeActivity();
    if (justWoke) return;            // wake reaction plays first
  }

  // ---- reactions (the Eilik personality map) ----
  if (te == TOUCH_HEAD_PAT) {
    if (now - lastPat > 10000) patCount = 0;
    lastPat = now;
    patCount++;
    if (patCount >= 4)      react(FACE_EMBARRASSED, 3500);  // overwhelmed
    else switch (random(3)) {  // Eilik loves head pats
      case 0: react(FACE_GIDDY); break;
      case 1: react(FACE_CHARMED); break;
      case 2: react(FACE_LOVING, 3500); break;
    }
  } else if (te == TOUCH_BODY) {
    // Affection escalation: giddy → charmed → loving (per datasheet)
    if (now - lastBodyTouch > 10000) bodyTouchCount = 0;  // reset after 10s idle
    lastBodyTouch = now;
    bodyTouchCount++;
    if (bodyTouchCount == 1)       react(FACE_GIDDY);          // first touch: giddy
    else if (bodyTouchCount <= 3)  react(FACE_CHARMED, 3500);  // repeated: charmed
    else                           react(FACE_LOVING, 4000);   // sustained: loving
  } else if (screenTap) {
    // poking the face: cute at first, annoying fast, then furious
    if (now - lastPoke > 8000) pokeCount = 0;
    lastPoke = now;
    pokeCount++;
    if (pokeCount <= 2)      react(FACE_WINK);
    else if (pokeCount <= 4) react(FACE_ANNOYED, 6500);
    else if (pokeCount == 5) react(FACE_GRUMPY, 3500);
    else if (pokeCount == 6) react(FACE_FURIOUS, 3500);
    else                     react(FACE_FUMING, 4000);
  } else if (me == MOTION_SHAKE) {
    if (now - lastShake > 12000) shakeCount = 0;
    lastShake = now;
    shakeCount++;
    // dizzy -> angry -> the sad chain (teary -> crying -> disappointed -> heartbroken)
    if (shakeCount == 1)      react(FACE_CONFUSED, 3500);
    else if (shakeCount == 2) react(FACE_FURIOUS, 4000);
    else if (shakeCount == 3) react(FACE_TEARY, 4000);
    else if (shakeCount == 4) react(FACE_CRYING, 5000);
    else if (shakeCount == 5) react(FACE_DISAPPOINTED, 4500);
    else                      react(FACE_HEARTBROKEN, 6000);  // enough!
  } else if (me == MOTION_KNOCK) {
    react(FACE_WONDER, 2500);
  } else if (me == MOTION_TILT) {
    react(FACE_SCARED, 60000);
    holdMood = true;                 // stays scared until put down
  } else if (me == MOTION_UPRIGHT && holdMood) {
    react(FACE_GIDDY);               // relieved
  }

  // ---- mood decay: a reaction mood expired ----
  // Only return to SMILE when still active. If an idle-decay stage
  // (bored/tired/yawning) is showing, leave that face alone — otherwise the
  // decay fights the idle machine and the yawn/tired face flickers repeatedly.
  if (!asleep && !holdMood && moodUntil && now > moodUntil) {
    moodUntil = 0;
    if (idleStage == 0) {
      faceSet(FACE_SMILE);
      logEmotion(FACE_SMILE);
      soundPlayFace(FACE_SMILE);   // smile is continuous: keeps humming softly
      if (!gestureBusy()) gesturePlayFace(FACE_SMILE);
    }
  }

  // ---- idle life: fidgets -> bored -> tired -> yawn -> sleep -> snore ----
  // Each stage fires exactly once (idleStage guard), so no repeats.
  unsigned long idle = now - lastInteraction;
  if (!asleep) {
    if (idle > IDLE_SLEEP_MS) {
      asleep = true;
      faceSet(FACE_SLEEPING);
      logEmotion(FACE_SLEEPING);
      soundPlayFace(FACE_SLEEPING);   // continuous: snores until woken
      servosSleep();                  // datasheet 32/38: torque off, truly limp
    } else if (idle > IDLE_YAWN_MS && idleStage < 3) {
      idleStage = 3;
      react(FACE_YAWNING, 4000);
    } else if (idle > IDLE_TIRED_MS && idleStage < 2) {
      idleStage = 2;
      react(FACE_TIRED, 4000);
    } else if (idle > IDLE_BORED_MS && idleStage < 1) {
      idleStage = 1;
      react(FACE_BORED, 8000);
    } else if (idle < IDLE_BORED_MS && now > nextFidget && !gestureBusy()) {
      nextFidget = now + random(18000, 40000);
      react(FIDGETS[random(FIDGET_COUNT)], 3000);   // Eilik entertains itself
    }
  }
}
