#include "EilikServos.h"
#include "Pins.h"

// Face names for the "which body movement" serial debug (Requirements.md).
static const char* const FACE_NAMES[FACE_COUNT] = {
  "SMILE","GIDDY","ECSTATIC","CHARMED","SPARKLE","WINK","STARRY","LAUGH","EAGER","ANTICIPATION",
  "POUT","TEARY","CRYING","DISAPPOINTED","HEARTBROKEN","GRUMPY","FURIOUS","FUMING","ANNOYED","FLUSHED",
  "QUESTIONING","INVESTIGATING","THOUGHTFUL","CONFUSED","WONDER","PLAYFUL","MISCHIEVOUS","CHEEKY","DERP","LOVING",
  "TIRED","SLEEPING","YAWNING","SLEEPY","SCARED","BORED","EMBARRASSED","HUNGRY"
};

// One step: speed per servo (-100..100, 0 = stop) held for ms. Every table's
// columns sum to zero (speed*ms) so the pose returns to neutral by construction.
// + = arm up / torso right; - = arm down / torso left.
struct GStep { int8_t armL, armR, torso; uint16_t ms; };

static const GStep G_GIDDY[]    = {{45,45,40,140},{-45,-45,-40,140},{0,0,-40,140},{0,0,40,140}};
static const GStep G_ECSTATIC[] = {{80,80,45,230},{-15,-15,-45,230},{15,15,-45,230},{-15,-15,45,230},{15,15,45,230},{-80,-80,-45,230}};
static const GStep G_CHARMED[]  = {{30,30,25,500},{0,0,0,1400},{-30,-30,-25,500}};
static const GStep G_SPARKLE[]  = {{70,70,60,280},{-10,-10,-60,280},{10,10,-60,280},{-70,-70,60,280}};
static const GStep G_WINK[]     = {{0,60,30,150},{0,-60,-30,150},{0,60,0,150},{0,-60,0,150}};
static const GStep G_LAUGH[]    = {{35,-35,25,110},{-35,35,-25,110},{35,-35,25,110},{-35,35,-25,110},{35,-35,25,110},{-35,35,-25,110}};
static const GStep G_EAGER[]    = {{40,40,35,300},{0,0,0,1200},{-40,-40,-35,300}};
static const GStep G_ANTICIP[]  = {{30,30,25,250},{0,0,0,1500},{-30,-30,-25,250}};
static const GStep G_POUT[]     = {{-30,-30,-25,300},{0,0,0,1400},{30,30,25,300}};
// TEARY/DISAPP/HEARTBRK/TIRED: arm columns flipped to lead UP-FRONT — the build
// can't droop the arms below their resting position (bench finding), so a
// negative-first "droop" swung them backward. Torso columns unchanged.
static const GStep G_TEARY[]    = {{35,35,-20,400},{-45,0,0,450},{45,0,0,450},{-35,-35,20,400}};
static const GStep G_CRYING[]   = {{50,50,10,300},{0,0,-10,300},{0,0,10,300},{0,0,-10,300},{0,0,10,300},{-50,-50,-10,300}};
static const GStep G_DISAPP[]   = {{60,60,-30,250},{0,0,0,2200},{-20,-20,10,750}};
static const GStep G_HEARTBRK[] = {{40,40,-35,700},{0,0,-35,700},{0,0,0,1800},{-40,-40,35,700},{0,0,35,700}};
static const GStep G_GRUMPY[]   = {{55,55,0,250},{0,0,0,2000},{-55,-55,0,250}};
static const GStep G_FURIOUS[]  = {{70,70,0,220},{12,12,15,90},{-12,-12,-15,90},{12,12,15,90},{-12,-12,-15,90},{-70,-70,0,220}};
static const GStep G_FUMING[]   = {{85,85,25,180},{-15,-15,-25,180},{15,15,-25,180},{-15,-15,25,180},{15,15,25,180},{-85,-85,-25,180}};
static const GStep G_ANNOYED[]  = {{65,65,-45,160},{0,0,0,400},{0,0,45,160},{0,0,0,900},{-65,-65,0,160}};
static const GStep G_FLUSHED[]  = {{60,60,20,200},{-30,-30,-20,200},{0,0,-20,200},{0,0,20,200},{-30,-30,0,200}};
static const GStep G_QUESTION[] = {{0,55,0,250},{0,0,0,1500},{0,-55,0,250}};
static const GStep G_INVESTIG[] = {{0,50,45,500},{0,0,-45,1000},{0,-50,45,500}};
static const GStep G_THOUGHT[]  = {{0,30,0,500},{0,0,0,1800},{0,-30,0,500}};
static const GStep G_CONFUSED[] = {{40,-40,50,180},{-40,40,-50,180},{40,-40,50,180},{-40,40,-50,180},{40,-40,50,180},{-40,40,-50,180}};
static const GStep G_WONDER[]   = {{35,35,25,450},{0,0,0,1600},{-35,-35,-25,450}};
static const GStep G_PLAYFUL[]  = {{0,50,30,160},{0,-50,-30,160},{0,50,30,160},{0,-50,-30,160},{0,50,30,160},{0,-50,-30,160}};
static const GStep G_MISCHIEF[] = {{45,45,-20,300},{8,-8,0,150},{-8,8,0,150},{8,-8,0,150},{-8,8,0,150},{-45,-45,20,300}};
static const GStep G_CHEEKY[]   = {{0,70,30,140},{0,0,-30,140},{0,0,0,300},{0,-70,0,140}};
static const GStep G_DERP[]     = {{50,-40,35,220},{-50,40,-15,220},{30,-25,-35,220},{-30,25,15,220}};
static const GStep G_LOVING[]   = {{45,45,20,400},{0,0,0,2000},{-45,-45,-20,400}};
static const GStep G_TIRED[]    = {{30,30,-20,600},{0,0,0,2000},{-30,-30,20,600}};
static const GStep G_YAWNING[]  = {{40,40,18,600},{0,0,0,1000},{-40,-40,-18,600}};
static const GStep G_SLEEPY[]   = {{25,25,-15,500},{-25,-25,0,500},{0,0,50,150}};
static const GStep G_SCARED[]   = {{80,80,-60,160},{0,0,30,160},{0,0,-15,160},{0,0,45,160},{0,0,0,500},{-40,-40,0,320}};
static const GStep G_BORED[]    = {{0,0,0,900},{30,0,0,250},{-30,0,0,250},{0,0,0,900},{0,30,0,250},{0,-30,0,250}};
static const GStep G_EMBARR[]   = {{60,-30,-35,250},{0,0,12,120},{0,0,-12,120},{0,0,0,1200},{-60,30,35,250}};
static const GStep G_HUNGRY[]   = {{45,45,25,300},{-45,-45,-25,300},{0,0,0,300},{45,45,25,300},{-45,-45,-25,300}};

struct GDef { const GStep* steps; uint8_t n; };
#define GD(s) {s, sizeof(s)/sizeof(GStep)}
static const GDef GESTURES[FACE_COUNT] = {
  /*SMILE*/{nullptr,0},      /*GIDDY*/GD(G_GIDDY),      /*ECSTATIC*/GD(G_ECSTATIC), /*CHARMED*/GD(G_CHARMED),  /*SPARKLE*/GD(G_SPARKLE),
  /*WINK*/GD(G_WINK),        /*STARRY*/GD(G_PLAYFUL),   /*LAUGH*/GD(G_LAUGH),       /*EAGER*/GD(G_EAGER),      /*ANTICIPATION*/GD(G_ANTICIP),
  /*POUT*/GD(G_POUT),        /*TEARY*/GD(G_TEARY),      /*CRYING*/GD(G_CRYING),     /*DISAPP*/GD(G_DISAPP),    /*HEARTBRK*/GD(G_HEARTBRK),
  /*GRUMPY*/GD(G_GRUMPY),    /*FURIOUS*/GD(G_FURIOUS),  /*FUMING*/GD(G_FUMING),     /*ANNOYED*/GD(G_ANNOYED),  /*FLUSHED*/GD(G_FLUSHED),
  /*QUESTION*/GD(G_QUESTION),/*INVESTIG*/GD(G_INVESTIG),/*THOUGHT*/GD(G_THOUGHT),   /*CONFUSED*/GD(G_CONFUSED),/*WONDER*/GD(G_WONDER),
  /*PLAYFUL*/GD(G_PLAYFUL),  /*MISCHIEF*/GD(G_MISCHIEF),/*CHEEKY*/GD(G_CHEEKY),     /*DERP*/GD(G_DERP),        /*LOVING*/GD(G_LOVING),
  /*TIRED*/GD(G_TIRED),      /*SLEEPING*/{nullptr,0},   /*YAWNING*/GD(G_YAWNING),   /*SLEEPY*/GD(G_SLEEPY),    /*SCARED*/GD(G_SCARED),
  /*BORED*/GD(G_BORED),      /*EMBARR*/GD(G_EMBARR),    /*HUNGRY*/GD(G_HUNGRY),
};

static const uint8_t SERVO_PINS[3] = {PIN_SERVO_ARM_L, PIN_SERVO_ARM_R, PIN_SERVO_TORSO};
static const int16_t STOP_TRIMS[3] = {SERVO_STOP_TRIM_ARM_L, SERVO_STOP_TRIM_ARM_R, SERVO_STOP_TRIM_TORSO};
// ---- bench-tuned calibration (from Body_Movement_Test) ----
static const int8_t  SERVO_DIR[3]  = {-1, +1, -1};        // travel direction per servo
static const float   REV_GAIN[3]   = {1.00f, 0.80f, 1.00f}; // trims the gravity-DOWN stroke
static const int8_t  REV_DOWN[3]   = {-1, -1, -1};        // which table sign is physically down
#define STOP_US   1500
#define RAMP_MS   70     // per-step slew time -> smooth, not jerky (non-blocking)

static const GDef* current = nullptr;
static uint8_t stepIdx = 0;
static unsigned long stepStart = 0, gestureStartTime = 0;
static float prevSpeed[3] = {0, 0, 0};   // speed at the start of the current step (slew source)

static void writeUs(uint8_t i, uint16_t us) {
  uint32_t duty = (uint32_t)us * 65535 / 20000;  // 50 Hz, 16-bit
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(SERVO_PINS[i], duty);
#else
  ledcWrite(i, duty);
#endif
}

// speed -100..100 (0 = stop). REV_GAIN trims the gravity-DOWN stroke (the one
// whose table sign matches REV_DOWN[i]); SERVO_DIR flips mounting direction.
static void setSpeed(uint8_t i, float speed) {
  speed = constrain(speed, -100.0f, 100.0f);
  bool goingDown = (speed * REV_DOWN[i]) > 0;
  float eff = goingDown ? speed * REV_GAIN[i] : speed;
  eff *= SERVO_DIR[i];
  writeUs(i, STOP_US + STOP_TRIMS[i] + (int32_t)lroundf(eff * SERVO_SPEED_RANGE_US / 100.0f));
}

static void allStop() { for (uint8_t i = 0; i < 3; i++) setSpeed(i, 0); }

// Drive the current step's speeds, slewed from prevSpeed over RAMP_MS then held.
static void applyStep(unsigned long elapsed) {
  const GStep& s = current->steps[stepIdx];
  const int8_t tgt[3] = { s.armL, s.armR, s.torso };
  float u = (elapsed >= RAMP_MS) ? 1.0f : (float)elapsed / RAMP_MS;
  for (uint8_t i = 0; i < 3; i++) setSpeed(i, prevSpeed[i] + (tgt[i] - prevSpeed[i]) * u);
}

void servosBegin() {
  for (uint8_t i = 0; i < 3; i++) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(SERVO_PINS[i], 50, 16);
#else
    ledcSetup(i, 50, 16);
    ledcAttachPin(SERVO_PINS[i], i);
#endif
  }
  allStop();
}

void gesturePlayFace(FaceExpr f) {
  current = nullptr;
  if (f < FACE_COUNT && GESTURES[f].n) {
    current = &GESTURES[f];
    stepIdx = 0;
    stepStart = millis();
    gestureStartTime = stepStart;
    for (uint8_t i = 0; i < 3; i++) prevSpeed[i] = 0;  // start from stop
    Serial.printf("[body] %s: %d steps\n", FACE_NAMES[f], current->n);
    applyStep(0);
    return;
  }
  allStop();  // faces with no movement (smile rest, sleeping)
}

void gestureUpdate() {
  if (!current) return;
  unsigned long now = millis();
  const GStep& s = current->steps[stepIdx];
  if (now - stepStart < s.ms) { applyStep(now - stepStart); return; }  // still in step -> slew/hold

  // step finished: lock its target as the slew source for the next step
  prevSpeed[0] = s.armL; prevSpeed[1] = s.armR; prevSpeed[2] = s.torso;
  stepIdx++;
  stepStart = now;
  if (stepIdx >= current->n) {
    Serial.printf("[body] done (%lums)\n", now - gestureStartTime);
    current = nullptr;
    allStop();
    for (uint8_t i = 0; i < 3; i++) prevSpeed[i] = 0;
    return;
  }
  applyStep(0);
}

bool gestureBusy() { return current != nullptr; }

void servosSleep() {
  current = nullptr;
  for (uint8_t i = 0; i < 3; i++) { writeUs(i, 0); prevSpeed[i] = 0; }  // no pulses -> de-energize
}
