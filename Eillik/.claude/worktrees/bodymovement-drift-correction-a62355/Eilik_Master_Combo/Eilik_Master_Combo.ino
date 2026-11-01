// ============================================================
// Eilik_Master_Combo — final integrated firmware (FULL-COMBO trigger model)
//   Same subsystems as Eilik_Master_Core (face / voice / calibrated body /
//   MPU6050 / touch), but the touch layer reads hold-duration, double-taps and
//   2-sensor combos (enhanced EilikTouch) to hit the datasheet's exact triggers:
//     chest tap        -> Giddy            chest short-hold  -> Anticipation
//     chest medium-hold-> Charmed          chest long-press  -> Loving
//     head tap         -> Wink             head double-tap   -> Playful
//     head + chest     -> Ecstatic         head + face       -> Mischievous
//     chest + face     -> Flushed          over-patting head -> Embarrassed
//     face poke        -> Cheeky -> Questioning -> Annoyed -> Grumpy -> Furious -> Fuming
//   Motion, mood-decay, idle-life and wake chains are identical to Core.
//
// Behavior per Eilik_Emotion_Movement_Datasheet.md + Requirements.md.
// Serial @115200: "emotion N/38 NAME" + body movement (EilikServos). Non-blocking.
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

static unsigned long moodUntil = 0;
static bool holdMood = false;
static void react(FaceExpr f, uint16_t holdMs = 3000) {
  faceSet(f); logEmotion(f); soundPlayFace(f); gesturePlayFace(f);
  moodUntil = millis() + holdMs; holdMood = false;
}

// ---- bookkeeping ----
static unsigned long lastInteraction = 0, lastScreenTap = 0, nextFidget = 0;
static unsigned long lastShake = 0, lastTilt = 0;
static uint8_t shakeCount = 0, tiltCount = 0;
static bool asleep = false;

// ---- combo/tap state (enhanced touch) ----
static unsigned long lastHeadPress = 0, lastBodyPress = 0, lastFaceTap = 0;
static unsigned long lastHeadTap = 0, lastBodyTap = 0;   // for double-tap
static uint8_t patCount = 0, pokeCount = 0;
static unsigned long lastPat = 0, lastPoke = 0;
static bool suppressHeadRel = false, suppressBodyRel = false;  // pad consumed by a combo
#define DOUBLE_TAP_MS 400
#define COMBO_MS      800     // face+pad "near-simultaneous" window
#define TAP_MAX_MS    250     // hold below this = a tap
#define HOLD_ANTICIP  800     // tap..this = short hold (Anticipation)
#define HOLD_CHARMED  2000    // ..this = medium hold (Charmed); above = Loving

// ---- idle life: stay lively, then wind down through a RANDOM decay cycle ----
#define IDLE_ACTIVE_MS   35000UL   // lively for this long with no interaction...
#define FIDGET_MIN_MS     7000UL   // ...doing a random funny move every 7..16 s
#define FIDGET_MAX_MS    16000UL

// fun random behaviours while awake & idle. Deliberately NO sleepy/sad faces
// here — yawning/tired/sleeping belong to the decay cycle only, never to a
// random fidget or a touch.
static const FaceExpr FIDGETS[] = {
  FACE_GIDDY, FACE_PLAYFUL, FACE_MISCHIEVOUS, FACE_DERP, FACE_CHEEKY, FACE_WINK,
  FACE_STARRY, FACE_SPARKLE, FACE_LAUGH, FACE_WONDER, FACE_QUESTIONING,
  FACE_INVESTIGATING, FACE_HUNGRY, FACE_ANTICIPATION, FACE_EAGER, FACE_THOUGHTFUL,
  FACE_ECSTATIC, FACE_FLUSHED
};
static const uint8_t FIDGET_COUNT = sizeof(FIDGETS) / sizeof(FIDGETS[0]);

// 3 different wind-down cycles; one is picked at random each time so the road to
// sleep is never the same twice. Each ends by dropping into SLEEPING.
struct DecayStep { FaceExpr face; uint16_t holdMs; };
static const DecayStep DECAY_A[] = { {FACE_BORED,6000}, {FACE_TIRED,7000}, {FACE_YAWNING,5000} };
static const DecayStep DECAY_B[] = { {FACE_THOUGHTFUL,5000}, {FACE_BORED,5000}, {FACE_SLEEPY,5000}, {FACE_YAWNING,4500} };
static const DecayStep DECAY_C[] = { {FACE_POUT,4500}, {FACE_BORED,5000}, {FACE_TIRED,6000}, {FACE_YAWNING,4500} };
struct DecayCycle { const DecayStep* s; uint8_t n; };
static const DecayCycle DECAYS[] = { {DECAY_A,3}, {DECAY_B,4}, {DECAY_C,4} };
static const uint8_t DECAY_COUNT = sizeof(DECAYS) / sizeof(DECAYS[0]);

static bool decayActive = false;
static uint8_t decayWhich = 0, decayIdx = 0;
static unsigned long decayStepUntil = 0;

static void markAwake() {
  lastInteraction = millis();
  decayActive = false; decayIdx = 0;
  nextFidget = millis() + random(FIDGET_MIN_MS, FIDGET_MAX_MS);
}

// ---- wake: touch/motion wakes STRAIGHT to the interaction. Yawning/Sleepy are
//      NEVER shown on wake — they belong to the decay cycle only (a touch that
//      made the robot look sleepy is exactly the annoyance we're removing). ----
static bool wakeIfAsleep() {
  if (!asleep) return false;
  asleep = false; decayActive = false; decayIdx = 0;
  faceSet(FACE_SMILE); logEmotion(FACE_SMILE);   // wake to a neutral face, no yawn
  markAwake();
  Serial.println("[wake] awake");
  return false;   // don't consume the event — let it play its own reaction
}

// ---- touch/combo front-end: returns true if it fired a reaction ----
static bool handleTouch(unsigned long now, const TouchEx& ev, bool faceTap) {
  // stamp presses (for combo windows + double-tap)
  if (ev.pressed & PAD_HEAD) lastHeadPress = now;
  if (ev.pressed & PAD_BODY) lastBodyPress = now;

  // ---- 2-sensor combos (highest touch priority) ----
  if ((ev.pressed & PAD_HEAD) && (ev.held & PAD_BODY)) {      // head while chest held
    suppressHeadRel = suppressBodyRel = true; react(FACE_ECSTATIC, 3500); return true;
  }
  if ((ev.pressed & PAD_BODY) && (ev.held & PAD_HEAD)) {      // chest while head held
    suppressHeadRel = suppressBodyRel = true; react(FACE_ECSTATIC, 3500); return true;
  }
  if (faceTap && ((ev.held & PAD_HEAD) || now - lastHeadPress < COMBO_MS)) {  // head + face
    suppressHeadRel = (ev.held & PAD_HEAD); react(FACE_MISCHIEVOUS, 4000); return true;
  }
  if (faceTap && ((ev.held & PAD_BODY) || now - lastBodyPress < COMBO_MS)) {  // chest + face
    suppressBodyRel = (ev.held & PAD_BODY); react(FACE_FLUSHED, 3500); return true;
  }

  // ---- head pad release: tap / double-tap / over-pat ----
  if (ev.released & PAD_HEAD) {
    if (suppressHeadRel) { suppressHeadRel = false; return false; }  // was part of a combo
    if (now - lastPat > 10000) patCount = 0;
    lastPat = now; patCount++;
    if (patCount >= 4) { react(FACE_EMBARRASSED, 3500); return true; }
    if (now - lastHeadTap < DOUBLE_TAP_MS) { lastHeadTap = now; react(FACE_PLAYFUL, 2500); return true; }
    lastHeadTap = now; react(FACE_WINK); return true;
  }

  // ---- chest pad release: duration ladder + double-tap ----
  if (ev.released & PAD_BODY) {
    if (suppressBodyRel) { suppressBodyRel = false; return false; }
    uint16_t h = ev.holdMs[1];
    if (h > HOLD_CHARMED)      { react(FACE_LOVING, 4000); return true; }   // long hug
    if (h > HOLD_ANTICIP)      { react(FACE_CHARMED, 3500); return true; }  // gentle hold
    if (h > TAP_MAX_MS)        { react(FACE_ANTICIPATION, 3000); return true; } // brief hold
    if (now - lastBodyTap < DOUBLE_TAP_MS) { lastBodyTap = now; react(FACE_PLAYFUL, 2500); return true; }
    lastBodyTap = now; react(FACE_GIDDY); return true;                      // quick tap
  }

  // ---- face (screen) poke: startle -> annoyance chain ----
  if (faceTap) {
    lastFaceTap = now;
    if (now - lastPoke > 8000) pokeCount = 0;
    lastPoke = now; pokeCount++;
    if (pokeCount == 1)      react(FACE_CHEEKY, 2500);
    else if (pokeCount == 2) react(FACE_QUESTIONING, 2500);
    else if (pokeCount <= 4) react(FACE_ANNOYED, 6000);
    else if (pokeCount == 5) react(FACE_GRUMPY, 3500);
    else if (pokeCount == 6) react(FACE_FURIOUS, 3500);
    else                     react(FACE_FUMING, 4000);
    return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nEilik_Master_Combo boot");

  screenTouch.begin();
  if (motionBegin()) Serial.println("MPU6050 ok (robot-frame axes)");
  else Serial.printf("MPU6050 not found (SDA=%d SCL=%d) - motion off\n", PIN_MPU_SDA, PIN_MPU_SCL);

  tft.init(); tft.setRotation(0); tft.fillScreen(TFT_BLACK);
  buffer.setColorDepth(8); buffer.createSprite(240, 240);
  faceBegin(&buffer);

  servosBegin();
  soundBegin();

  faceSet(FACE_SMILE); logEmotion(FACE_SMILE);
  soundPlayBoot();
  gesturePlayFace(FACE_ECSTATIC);
  markAwake();
}

void loop() {
  unsigned long now = millis();
  faceUpdate();
  gestureUpdate();

  // ---- gather events ----
  TouchEx ev = touchUpdate();
  MotionEvent me = motionUpdate();
  bool faceTap = false;
  if (screenTouch.available() && now - lastScreenTap > 600) { lastScreenTap = now; faceTap = true; }

  // print where a touch landed / what motion fired (serial debug)
  if (ev.pressed & PAD_HEAD) Serial.println("TOUCH: HEAD (top pad)");
  if (ev.pressed & PAD_BODY) Serial.println("TOUCH: BODY (chest pad)");
  if (faceTap)               Serial.println("TOUCH: FACE (screen)");
  if (me == MOTION_TILT)         Serial.println("IMU: TILT (picked up)");
  else if (me == MOTION_SHAKE)   Serial.println("IMU: SHAKE");
  else if (me == MOTION_KNOCK)   Serial.println("IMU: KNOCK");
  else if (me == MOTION_UPRIGHT) Serial.println("IMU: UPRIGHT (put down)");

  bool anyEvent = ev.pressed || ev.released || faceTap ||
                  me == MOTION_KNOCK || me == MOTION_SHAKE || me == MOTION_TILT;
  if (anyEvent) { wakeIfAsleep(); markAwake(); }

  // ---- motion first (scary priority, datasheet §4) ----
  bool reacted = false;
  if (me == MOTION_TILT) {
    if (now - lastTilt > 8000) tiltCount = 0;
    lastTilt = now; tiltCount++;
    if (tiltCount >= 2) react(FACE_ANNOYED, 4000);
    else { react(FACE_SCARED, 60000); holdMood = true; }
    reacted = true;
  } else if (me == MOTION_UPRIGHT && holdMood) { react(FACE_GIDDY); reacted = true; }
  else if (me == MOTION_SHAKE) {
    if (now - lastShake > 12000) shakeCount = 0;
    lastShake = now; shakeCount++;
    if (shakeCount == 1)      react(FACE_CONFUSED, 3500);
    else if (shakeCount == 2) react(FACE_SCARED, 4000);
    else                      react(FACE_CRYING, 5000);
    reacted = true;
  } else if (me == MOTION_KNOCK) { react(FACE_WONDER, 2500); reacted = true; }

  // ---- then touch combos / taps ----
  if (!reacted) handleTouch(now, ev, faceTap);

  // ---- mood decay: a reaction/fidget mood expired -> back to a plain smile.
  //      (Disabled during a decay cycle, which owns the face itself.) ----
  if (!asleep && !holdMood && !decayActive && moodUntil && now > moodUntil) {
    moodUntil = 0;
    faceSet(FACE_SMILE); logEmotion(FACE_SMILE); soundPlayFace(FACE_SMILE);
    if (!gestureBusy()) gesturePlayFace(FACE_SMILE);
  }

  // ---- idle life: lively random fidgets -> a RANDOM wind-down cycle -> sleep --
  if (!asleep) {
    unsigned long idle = now - lastInteraction;
    if (decayActive) {                               // stepping through a wind-down
      if (now >= decayStepUntil) {
        const DecayCycle& c = DECAYS[decayWhich];
        if (decayIdx >= c.n) {                        // cycle finished -> fall asleep
          asleep = true; decayActive = false;
          faceSet(FACE_SLEEPING); logEmotion(FACE_SLEEPING);
          soundPlayFace(FACE_SLEEPING); servosSleep();
        } else {
          const DecayStep& st = c.s[decayIdx++];
          react(st.face, st.holdMs);
          decayStepUntil = now + st.holdMs;
        }
      }
    } else if (!holdMood && idle > IDLE_ACTIVE_MS) { // been quiet a while -> wind down
      decayActive = true; decayWhich = random(DECAY_COUNT); decayIdx = 0;
      decayStepUntil = now;                          // play the first step immediately
      Serial.printf("[decay] wind-down cycle %c\n", (char)('A' + decayWhich));
    } else if (!holdMood && now > nextFidget && !gestureBusy()) { // random funny idle move
      nextFidget = now + random(FIDGET_MIN_MS, FIDGET_MAX_MS);
      react(FIDGETS[random(FIDGET_COUNT)], 2500);
    }
  }
}
