#include "EilikServos.h"
#include "Pins.h"

// Face expression names for serial debug output
static const char* const FACE_NAMES[FACE_COUNT] = {
  "SMILE","GIDDY","ECSTATIC","CHARMED","SPARKLE","WINK","STARRY","LAUGH","EAGER","ANTICIPATION",
  "POUT","TEARY","CRYING","DISAPPOINTED","HEARTBROKEN","GRUMPY","FURIOUS","FUMING","ANNOYED","FLUSHED",
  "QUESTIONING","INVESTIGATING","THOUGHTFUL","CONFUSED","WONDER","PLAYFUL","MISCHIEVOUS","CHEEKY","DERP","LOVING",
  "TIRED","SLEEPING","YAWNING","SLEEPY","SCARED","BORED","EMBARRASSED","HUNGRY"
};

// One step: speed per servo (-100..100, 0 = stop) held for ms.
// Arms: + = up, - = down. Torso: + = right, - = left (swap signs at the
// linkage if your build runs mirrored). Every table sums to zero
// speed*ms per column so the pose returns to neutral by construction.
struct GStep { int8_t armL, armR, torso; uint16_t ms; };

// ---- 38 movements from Eilik_Emotion_Movement_Datasheet.md ----
// Angles in comments are the datasheet targets; with continuous servos they
// are approximations — tune ms/speed on the bench to match your throw.
// Row 1: Joy & Excitement
//  1 SMILE: resting state — servos stopped (datasheet's idle sway skipped:
//    a perpetual micro-sway on continuous servos is pure drift).
static const GStep G_GIDDY[]    = {{45,45,40,140},{-45,-45,-40,140},{0,0,-40,140},{0,0,40,140}};                      //  2 single arm bounce + one ±15° torso wiggle
static const GStep G_ECSTATIC[] = {{80,80,45,230},{-15,-15,-45,230},{15,15,-45,230},{-15,-15,45,230},{15,15,45,230},{-80,-80,-45,230}}; //  3 arms shoot up + victory shake, ±45° double sweep
static const GStep G_CHARMED[]  = {{30,30,25,500},{0,0,0,1400},{-30,-30,-25,500}};                                    //  4 slow "aww" to chest, ~20° turn, hold
static const GStep G_SPARKLE[]  = {{70,70,60,280},{-10,-10,-60,280},{10,10,-60,280},{-70,-70,60,280}};                //  5 ta-da sweep + wrist flick, ±60° decelerating
static const GStep G_WINK[]     = {{0,60,30,150},{0,-60,-30,150},{0,60,0,150},{0,-60,0,150}};                         //  6 one-arm quick wave, ~15° turn + snap back
static const GStep G_LAUGH[]    = {{35,-35,25,110},{-35,35,-25,110},{35,-35,25,110},{-35,35,-25,110},{35,-35,25,110},{-35,35,-25,110}}; //  8 alternating shimmy, ±10° @ ~3Hz
static const GStep G_EAGER[]    = {{40,40,35,300},{0,0,0,1200},{-40,-40,-35,300}};                                    //  9 lean-in arms, ~30° toward source, hold
static const GStep G_ANTICIP[]  = {{30,30,25,250},{0,0,0,1500},{-30,-30,-25,250}};                                    // 10 rise + pause mid-motion (held incomplete turn)
// Row 2: Sadness & Anger
static const GStep G_POUT[]     = {{-30,-30,-25,300},{0,0,0,1400},{30,30,25,300}};                                    // 11 "hmph" arms cross low, ~20° away, hold
static const GStep G_TEARY[]    = {{-35,-35,-20,400},{45,0,0,450},{-45,0,0,450},{35,35,20,400}};                      // 12 droop, one arm wipes eye, ~15° drift
static const GStep G_CRYING[]   = {{50,50,10,300},{0,0,-10,300},{0,0,10,300},{0,0,-10,300},{0,0,10,300},{-50,-50,-10,300}}; // 13 cover face, ±5° tremor near center
static const GStep G_DISAPP[]   = {{-60,-60,-30,250},{0,0,0,2200},{20,20,10,750}};                                    // 14 sudden arm drop, settle ~25° off, slow creep back
static const GStep G_HEARTBRK[] = {{-40,-40,-35,700},{0,0,-35,700},{0,0,0,1800},{40,40,35,700},{0,0,35,700}};         // 15 limp droop, slow turn toward limit, hold
static const GStep G_GRUMPY[]   = {{55,55,0,250},{0,0,0,2000},{-55,-55,0,250}};                                       // 16 arms cross firm, torso locked center
static const GStep G_FURIOUS[]  = {{70,70,0,220},{12,12,15,90},{-12,-12,-15,90},{12,12,15,90},{-12,-12,-15,90},{-70,-70,0,220}};        // 17 stiff raise + vibrate, ±8° tremor
static const GStep G_FUMING[]   = {{85,85,25,180},{-15,-15,-25,180},{15,15,-25,180},{-15,-15,25,180},{15,15,25,180},{-85,-85,-25,180}}; // 18 sharp overhead vent, ±15° faster tremor
static const GStep G_ANNOYED[]  = {{65,65,-45,160},{0,0,0,400},{0,0,45,160},{0,0,0,900},{-65,-65,0,160}};             // 19 hands-on-hips snap, ~15° away + snap back, rigid
static const GStep G_FLUSHED[]  = {{60,60,20,200},{-30,-30,-20,200},{0,0,-20,200},{0,0,20,200},{-30,-30,0,200}};      // 20 cover face, retreat halfway, small wobble
// Row 3: Curiosity, Playful & Affection
static const GStep G_QUESTION[] = {{0,55,0,250},{0,0,0,1500},{0,-55,0,250}};                                          // 21 one arm palm-up "what?", torso static
static const GStep G_INVESTIG[] = {{0,50,45,500},{0,0,-45,1000},{0,-50,45,500}};                                      // 22 pointing arm, slow full ±90° scan
static const GStep G_THOUGHT[]  = {{0,30,0,500},{0,0,0,1800},{0,-30,0,500}};                                          // 23 arm to chin, hold, torso static
static const GStep G_CONFUSED[] = {{40,-40,50,180},{-40,40,-50,180},{40,-40,50,180},{-40,40,-50,180},{40,-40,50,180},{-40,40,-50,180}}; // 24 alternating arm circles, ±30° fast wobble
static const GStep G_WONDER[]   = {{35,35,25,450},{0,0,0,1600},{-35,-35,-25,450}};                                    // 25 slow amazed rise, ~20° turn, hold
static const GStep G_PLAYFUL[]  = {{0,50,30,160},{0,-50,-30,160},{0,50,30,160},{0,-50,-30,160},{0,50,30,160},{0,-50,-30,160}};          // 26 one-arm wave, ±20° bouncy rock (~2Hz)
static const GStep G_MISCHIEF[] = {{45,45,-20,300},{8,-8,0,150},{-8,8,0,150},{8,-8,0,150},{-8,8,0,150},{-45,-45,20,300}};               // 27 scheming hand-rub, slow ~20° sneak turn
static const GStep G_CHEEKY[]   = {{0,70,30,140},{0,0,-30,140},{0,0,0,300},{0,-70,0,140}};                            // 28 cheeky flick up, ~15° snap + back, drop
static const GStep G_DERP[]     = {{50,-40,35,220},{-50,40,-15,220},{30,-25,-35,220},{-30,25,15,220}};                // 29 floppy async flops, uneven stagger
static const GStep G_LOVING[]   = {{45,45,20,400},{0,0,0,2000},{-45,-45,-20,400}};                                    // 30 self-hug sweep, ~15-20° turn, hold
// Row 4: Fatigue & Reactive
static const GStep G_TIRED[]    = {{-30,-30,-20,600},{0,0,0,2000},{30,30,20,600}};                                    // 31 slow droop, low-energy drift, hold
// 32 SLEEPING: no table — servosSleep() de-energizes everything (datasheet: torque off).
static const GStep G_YAWNING[]  = {{40,40,18,600},{0,0,0,1000},{-40,-40,-18,600}};                                    // 33 slow stretch up+out, gentle side turn, hold 1s
static const GStep G_SLEEPY[]   = {{25,25,-15,500},{-25,-25,0,500},{0,0,50,150}};                                     // 34 heavy half-lift settling, drift + corrective snap
static const GStep G_SCARED[]   = {{80,80,-60,160},{0,0,30,160},{0,0,-15,160},{0,0,45,160},{0,0,0,500},{-40,-40,0,320}}; // 35 fast defensive flinch, snap away + decaying tremor
static const GStep G_BORED[]    = {{0,0,0,900},{30,0,0,250},{-30,0,0,250},{0,0,0,900},{0,30,0,250},{0,-30,0,250}};    // 36 loose hang, occasional single-arm shrugs
static const GStep G_EMBARR[]   = {{60,-30,-35,250},{0,0,12,120},{0,0,-12,120},{0,0,0,1200},{-60,30,35,250}};         // 37 one arm covers face, ~20-30° away + fast tremor
static const GStep G_HUNGRY[]   = {{45,45,25,300},{-45,-45,-25,300},{0,0,0,300},{45,45,25,300},{-45,-45,-25,300}};    // 38 two "gimme" reach cycles toward the food source

struct GDef { const GStep* steps; uint8_t n; };
#define GD(s) {s, sizeof(s)/sizeof(GStep)}
static const GDef GESTURES[FACE_COUNT] = {
  /*SMILE*/{nullptr,0},  /*GIDDY*/GD(G_GIDDY),      /*ECSTATIC*/GD(G_ECSTATIC), /*CHARMED*/GD(G_CHARMED),  /*SPARKLE*/GD(G_SPARKLE),
  /*WINK*/GD(G_WINK),    /*STARRY*/GD(G_PLAYFUL),   /*LAUGH*/GD(G_LAUGH),       /*EAGER*/GD(G_EAGER),      /*ANTICIPATION*/GD(G_ANTICIP),
  /*POUT*/GD(G_POUT),    /*TEARY*/GD(G_TEARY),      /*CRYING*/GD(G_CRYING),     /*DISAPP*/GD(G_DISAPP),    /*HEARTBRK*/GD(G_HEARTBRK),
  /*GRUMPY*/GD(G_GRUMPY),/*FURIOUS*/GD(G_FURIOUS),  /*FUMING*/GD(G_FUMING),     /*ANNOYED*/GD(G_ANNOYED),  /*FLUSHED*/GD(G_FLUSHED),
  /*QUESTION*/GD(G_QUESTION),/*INVESTIG*/GD(G_INVESTIG),/*THOUGHT*/GD(G_THOUGHT),/*CONFUSED*/GD(G_CONFUSED),/*WONDER*/GD(G_WONDER),
  /*PLAYFUL*/GD(G_PLAYFUL),/*MISCHIEF*/GD(G_MISCHIEF),/*CHEEKY*/GD(G_CHEEKY),   /*DERP*/GD(G_DERP),        /*LOVING*/GD(G_LOVING),
  /*TIRED*/GD(G_TIRED),  /*SLEEPING*/{nullptr,0},   /*YAWNING*/GD(G_YAWNING),   /*SLEEPY*/GD(G_SLEEPY),    /*SCARED*/GD(G_SCARED),
  /*BORED*/GD(G_BORED),  /*EMBARR*/GD(G_EMBARR),    /*HUNGRY*/GD(G_HUNGRY),
};
// STARRY (7/38) is missing from the datasheet ("not captured") — it reuses
// Playful's motion until that row gets specified.

static const uint8_t SERVO_PINS[3] = {PIN_SERVO_ARM_L, PIN_SERVO_ARM_R, PIN_SERVO_TORSO};
static const int16_t STOP_TRIMS[3] = {SERVO_STOP_TRIM_ARM_L, SERVO_STOP_TRIM_ARM_R, SERVO_STOP_TRIM_TORSO};

static const GDef* current = nullptr;
static uint8_t stepIdx = 0;
static unsigned long stepStart = 0;
static unsigned long gestureStartTime = 0;

static void writeUs(uint8_t i, uint16_t us) {
  uint32_t duty = (uint32_t)us * 65535 / 20000;  // 50 Hz, 16-bit
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(SERVO_PINS[i], duty);
#else
  ledcWrite(i, duty);  // channel i (attached in servosBegin)
#endif
}

static void writeSpeed(uint8_t i, int8_t speed) {   // immediate PWM write
  writeUs(i, 1500 + STOP_TRIMS[i] + (int32_t)speed * SERVO_SPEED_RANGE_US / 100);
}

// ---- speed slew: ramp toward the commanded speed instead of stepping to it
// instantly. A continuous servo's current spikes when it accelerates/reverses;
// spreading each speed change over a few ms lowers the peak draw, so the shared
// 5V rail sags less and the audio amp crackles less. Tune SERVO_SLEW_PER_MS in
// Pins.h (0 = old instant behaviour). ----
static int8_t  targetSp[3] = {0, 0, 0};
static float   curSp[3]    = {0, 0, 0};
static unsigned long lastSlew = 0;
static bool    energized = true;      // false while sleeping (torque off)

static void setStep(const GStep& s) { // set targets only; servosUpdate() ramps
  targetSp[0] = s.armL; targetSp[1] = s.armR; targetSp[2] = s.torso;
  energized = true;
}

static void allStop() {               // ramp to stop, but keep holding (energized)
  targetSp[0] = targetSp[1] = targetSp[2] = 0;
  energized = true;
}

void servosUpdate() {                 // call every loop(); cheap, non-blocking
  if (!energized) return;             // limp while asleep
  unsigned long now = millis();
  if (lastSlew == 0) lastSlew = now;
  unsigned long dt = now - lastSlew;
  if (dt < 4) return;                 // ~250 Hz update is plenty
  lastSlew = now;
#if SERVO_SLEW_PER_MS <= 0
  for (uint8_t i = 0; i < 3; i++) { curSp[i] = targetSp[i]; writeSpeed(i, targetSp[i]); }
#else
  float maxStep = (float)SERVO_SLEW_PER_MS * dt;
  for (uint8_t i = 0; i < 3; i++) {
    float d = (float)targetSp[i] - curSp[i];
    if (d >  maxStep) d =  maxStep;
    if (d < -maxStep) d = -maxStep;
    curSp[i] += d;
    writeSpeed(i, (int8_t)lroundf(curSp[i]));
  }
#endif
}

void servosBegin() {
  for (uint8_t i = 0; i < 3; i++) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(SERVO_PINS[i], 50, 16);
#else
    ledcSetup(i, 50, 16);
    ledcAttachPin(SERVO_PINS[i], i);
#endif
    curSp[i] = 0; targetSp[i] = 0;
    writeSpeed(i, 0);                 // valid stop pulse from the first frame
  }
  energized = true;
}

void gesturePlayFace(FaceExpr f) {
  current = nullptr;
  if (f < FACE_COUNT && GESTURES[f].n) {
    current = &GESTURES[f];
    stepIdx = 0;
    stepStart = millis();
    gestureStartTime = stepStart;
    Serial.printf("[Gesture] Start: emotion=%d (%s), steps=%d\n", f, FACE_NAMES[f], current->n);
    setStep(current->steps[0]);
    return;
  }
  allStop();  // faces with no movement (smile rest, sleeping)
}

void gestureUpdate() {
  if (!current) return;
  const GStep& s = current->steps[stepIdx];
  if (millis() - stepStart < s.ms) return;
  stepIdx++;
  stepStart = millis();
  if (stepIdx >= current->n) {
    unsigned long elapsed = millis() - gestureStartTime;
    Serial.printf("[Gesture] Complete: elapsed=%lums\n", elapsed);
    current = nullptr; allStop(); return;
  }
  const GStep& next = current->steps[stepIdx];
  Serial.printf("[Gesture] Step %d/%d: armL=%d armR=%d torso=%d duration=%dms\n",
                stepIdx + 1, current->n, next.armL, next.armR, next.torso, next.ms);
  setStep(next);
}

bool gestureBusy() { return current != nullptr; }

void servosSleep() {
  current = nullptr;
  energized = false;                                // servosUpdate() stops writing
  targetSp[0] = targetSp[1] = targetSp[2] = 0;
  curSp[0] = curSp[1] = curSp[2] = 0;
  for (uint8_t i = 0; i < 3; i++) writeUs(i, 0);    // no pulses -> servos de-energize
}
