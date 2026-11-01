// Body_Movement_Test.ino — OPEN-LOOP body-movement test for the 3 installed
// 360-degree continuous-rotation MG90S servos (left arm, right arm, torso).
//
// THE HARD TRUTH ABOUT THIS HARDWARE: a 3-wire continuous-rotation servo has
// NO position feedback and NO home reference. It only knows speed + direction.
// So there is no way to command an angle or truly return to a fixed home, and
// some drift is unavoidable. What we CAN do is make every gesture a net-zero
// excursion (equal motion out and back, so it returns to where it started) and
// then calibrate away the two things that make out-and-back NOT cancel:
//
//   • STOP_TRIM  — "stop" (1500us) isn't exactly stop; the servo creeps. This
//                  slow creep is the biggest drift source. Tune each servo
//                  until speed 0 truly holds still.  ← do this FIRST.
//   • REV_GAIN   — an arm rises slower than it falls (gravity), so +40 up then
//                  -40 down doesn't cancel. This scales the reverse throw per
//                  servo so an out-and-back actually nets zero.  ← tune second.
//
// HOME is simply "wherever the arms are at power-up." On boot the servos go
// LIMP for HOME_SET_MS so you can hand-position them; after that they hold
// stop and gestures play as relative moves that come back to that pose. It
// will still drift slowly over many cycles — that's the motor, not the code.
//
// SMOOTHNESS: every speed change is slew-ramped (RAMP_MS) instead of snapping,
// which removes the jerk and makes the transients repeat more consistently
// (which also helps drift a little). Raise RAMP_MS for softer, lower for snappier.

#include <Arduino.h>

// ---- Pins (match Eilik_Bot/Pins.h; head servo not fitted) ----
static const uint8_t SERVO_PIN[3] = { 16, 17, 15 };  // armL, armR, torso
static const char* const AXIS[3]  = { "armL", "armR", "torso" };
// Head touch pad steps through the movements (Eilik_Bot/Pins.h, ESP32 T9).
#define PIN_TOUCH_HEAD    32
#define TOUCH_HEAD_THRESH 950   // touchRead DROPS when touched -> touched if < this

// ---- Drive: 1500us = stop, +/-SPEED_RANGE_US = full speed ----
#define STOP_US        1500
#define SPEED_RANGE_US 400

// ================= CALIBRATION KNOBS (this is the whole game) =================
// 1) STOP_TRIM: watch each servo at rest. If it creeps, nudge its trim (us,
//    roughly -60..+60) until it holds still. Biggest drift reducer.
static const int16_t STOP_TRIM[3] = { 0, 0, 0 };
// 2) REV_GAIN: trims the throw on the gravity-assisted DOWN stroke so it can't
//    overshoot the up stroke. Watch one arm over ~20 WAVE cycles: if it walks
//    DOWN, lower its gain (0.90, 0.85...); if it walks UP, raise it past 1.0.
//    If lowering makes drift WORSE, REV_GAIN is hitting the wrong stroke ->
//    flip that servo's REV_DOWN (below), then tune again.
static const float REV_GAIN[3] = { 1.00f, 0.80f, 1.00f };  // down-stroke throw scale
// 3) REV_DOWN: which TABLE sign is PHYSICALLY down for each servo (the stroke
//    REV_GAIN trims). Normally down = table '-' (-1). A SERVO_DIR flip can
//    invert up/down, so a flipped servo often needs +1. Verify by eye.
static const int8_t REV_DOWN[3] = { -1, -1, -1 };  // measured: down = table '-' on all three
// 4) SERVO_DIR: travel direction per servo. Flip a sign if a joint runs the
//    wrong way (arm drops when it should rise, torso turns the wrong side).
static const int8_t SERVO_DIR[3] = { -1, +1, -1 };  // armL, armR, torso  (-1 = reversed)
// MODE: set to 1 to just hold every servo at stop forever — watch which arm
// creeps and nudge its STOP_TRIM until it's dead still. Set back to 0 to run.
#define CAL_STOP 0
// =============================================================================

#define RAMP_MS      70    // slew time per speed change -> smooth, not jerky
#define TICK_MS      15    // slew update period (~66 Hz)
#define HOME_SET_MS  5000  // boot window: servos limp so you can hand-set home

static float speedNow[3] = { 0, 0, 0 };

static void writeUs(uint8_t i, uint16_t us) {
  uint32_t duty = (uint32_t)us * 65535 / 20000;  // 50 Hz, 16-bit
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(SERVO_PIN[i], duty);
#else
  ledcWrite(i, duty);
#endif
}

// speed -100..100 (0 = stop). REV_GAIN trims the gravity-assisted DOWN stroke
// so an up/down pair cancels. The down stroke is the table sign matching
// REV_DOWN[i] (decoupled from SERVO_DIR, since a dir flip can invert up/down).
static void setSpeed(uint8_t i, float speed) {
  speed = constrain(speed, -100.0f, 100.0f);
  bool goingDown = (speed * REV_DOWN[i]) > 0;          // same sign as "down" for this servo
  float eff = goingDown ? speed * REV_GAIN[i] : speed; // trim only the down stroke
  eff *= SERVO_DIR[i];                                  // mounting direction, last
  writeUs(i, STOP_US + STOP_TRIM[i] + (int32_t)lroundf(eff * SPEED_RANGE_US / 100.0f));
}

static void limp(uint16_t ms) {                 // no pulses -> arms free to hand-set
  for (uint8_t i = 0; i < 3; i++) { writeUs(i, 0); speedNow[i] = 0; }
  delay(ms);
}

// Slew from the current speeds to the target speeds over RAMP_MS, then hold the
// targets for the rest of ms. This is the smoothing layer.
static void stepTo(int8_t tL, int8_t tR, int8_t tT, uint16_t ms) {
  const float tgt[3] = { (float)tL, (float)tR, (float)tT };
  const float src[3] = { speedNow[0], speedNow[1], speedNow[2] };
  uint16_t ramp = ms < RAMP_MS ? ms : RAMP_MS;
  uint32_t t0 = millis(), t;
  while ((t = millis() - t0) < ramp) {
    float u = (float)t / ramp;
    for (uint8_t i = 0; i < 3; i++) setSpeed(i, src[i] + (tgt[i] - src[i]) * u);
    delay(TICK_MS);
  }
  for (uint8_t i = 0; i < 3; i++) { setSpeed(i, tgt[i]); speedNow[i] = tgt[i]; }
  if (ms > ramp) delay(ms - ramp);
}

// One step: speed per servo held for ms. Every gesture's columns SUM TO ZERO
// (speed*ms), so it returns to the starting pose by construction. + = arm up /
// torso right; - = arm down / torso left.
struct GStep { int8_t armL, armR, torso; uint16_t ms; };

// ---- All 38 emotion movements, ported from Eilik_Bot/EilikServos.cpp ----
// Same net-zero speed*ms tables; the calibration knobs above apply to all.
static const GStep G_GIDDY[]    = {{45,45,40,140},{-45,-45,-40,140},{0,0,-40,140},{0,0,40,140}};
static const GStep G_ECSTATIC[] = {{80,80,45,230},{-15,-15,-45,230},{15,15,-45,230},{-15,-15,45,230},{15,15,45,230},{-80,-80,-45,230}};
static const GStep G_CHARMED[]  = {{30,30,25,500},{0,0,0,1400},{-30,-30,-25,500}};
static const GStep G_SPARKLE[]  = {{70,70,60,280},{-10,-10,-60,280},{10,10,-60,280},{-70,-70,60,280}};
static const GStep G_WINK[]     = {{0,60,30,150},{0,-60,-30,150},{0,60,0,150},{0,-60,0,150}};
static const GStep G_LAUGH[]    = {{35,-35,25,110},{-35,35,-25,110},{35,-35,25,110},{-35,35,-25,110},{35,-35,25,110},{-35,35,-25,110}};
static const GStep G_EAGER[]    = {{40,40,35,300},{0,0,0,1200},{-40,-40,-35,300}};
static const GStep G_ANTICIP[]  = {{30,30,25,250},{0,0,0,1500},{-30,-30,-25,250}};
static const GStep G_POUT[]     = {{-30,-30,-25,300},{0,0,0,1400},{30,30,25,300}};
static const GStep G_TEARY[]    = {{35,35,-20,400},{-45,0,0,450},{45,0,0,450},{-35,-35,20,400}};  // arms lead up-front (build can't droop below rest)
static const GStep G_CRYING[]   = {{50,50,10,300},{0,0,-10,300},{0,0,10,300},{0,0,-10,300},{0,0,10,300},{-50,-50,-10,300}};
static const GStep G_DISAPP[]   = {{60,60,-30,250},{0,0,0,2200},{-20,-20,10,750}};  // arms lead up-front
static const GStep G_HEARTBRK[] = {{40,40,-35,700},{0,0,-35,700},{0,0,0,1800},{-40,-40,35,700},{0,0,35,700}};  // arms lead up-front
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
static const GStep G_TIRED[]    = {{30,30,-20,600},{0,0,0,2000},{-30,-30,20,600}};  // arms lead up-front
static const GStep G_YAWNING[]  = {{40,40,18,600},{0,0,0,1000},{-40,-40,-18,600}};
static const GStep G_SLEEPY[]   = {{25,25,-15,500},{-25,-25,0,500},{0,0,50,150}};
static const GStep G_SCARED[]   = {{80,80,-60,160},{0,0,30,160},{0,0,-15,160},{0,0,45,160},{0,0,0,500},{-40,-40,0,320}};
static const GStep G_BORED[]    = {{0,0,0,900},{30,0,0,250},{-30,0,0,250},{0,0,0,900},{0,30,0,250},{0,-30,0,250}};
static const GStep G_EMBARR[]   = {{60,-30,-35,250},{0,0,12,120},{0,0,-12,120},{0,0,0,1200},{-60,30,35,250}};
static const GStep G_HUNGRY[]   = {{45,45,25,300},{-45,-45,-25,300},{0,0,0,300},{45,45,25,300},{-45,-45,-25,300}};

struct Gesture { const char* name; const char* desc; const GStep* steps; uint8_t n; };
#define GD(nm, ds, arr) { nm, ds, arr, sizeof(arr) / sizeof(GStep) }
#define REST(nm, ds)    { nm, ds, nullptr, 0 }
// All 38 in FaceExpr order (SMILE & SLEEPING are idle/rest = just hold home).
static const Gesture SHOW[] = {
  REST("SMILE",       "resting — idle, no body movement"),
  GD("GIDDY",         "single arm bounce + a small torso wiggle", G_GIDDY),
  GD("ECSTATIC",      "arms shoot up + victory shake, double torso sweep", G_ECSTATIC),
  GD("CHARMED",       "slow 'aww' hands to chest, slight turn, hold", G_CHARMED),
  GD("SPARKLE",       "ta-da sweep + wrist flick, decelerating", G_SPARKLE),
  GD("WINK",          "one-arm quick wave, slight turn + snap back", G_WINK),
  GD("STARRY",        "(reuses Playful) one-arm wave, bouncy rock", G_PLAYFUL),
  GD("LAUGH",         "alternating arm shimmy, fast", G_LAUGH),
  GD("EAGER",         "lean-in arms toward source, hold", G_EAGER),
  GD("ANTICIPATION",  "rise + pause mid-motion, held", G_ANTICIP),
  GD("POUT",          "arms cross low 'hmph', turn away, hold", G_POUT),
  GD("TEARY",         "arms lift, one wipes eye, settle back", G_TEARY),
  GD("CRYING",        "cover face, small tremor near center", G_CRYING),
  GD("DISAPPOINTED",  "arms raise up front, hold, slow settle", G_DISAPP),
  GD("HEARTBROKEN",   "arms raise, slow torso turn, hold", G_HEARTBRK),
  GD("GRUMPY",        "arms cross firm, torso locked center", G_GRUMPY),
  GD("FURIOUS",       "stiff raise + vibrate tremor", G_FURIOUS),
  GD("FUMING",        "sharp overhead vent, faster tremor", G_FUMING),
  GD("ANNOYED",       "hands-on-hips snap + snap back, rigid", G_ANNOYED),
  GD("FLUSHED",       "cover face, retreat halfway, small wobble", G_FLUSHED),
  GD("QUESTIONING",   "one arm palm-up 'what?', torso static", G_QUESTION),
  GD("INVESTIGATING", "pointing arm, slow wide scan", G_INVESTIG),
  GD("THOUGHTFUL",    "arm to chin, hold", G_THOUGHT),
  GD("CONFUSED",      "alternating arm circles, fast wobble", G_CONFUSED),
  GD("WONDER",        "slow amazed rise, turn, hold", G_WONDER),
  GD("PLAYFUL",       "one-arm wave, bouncy rock (~2Hz)", G_PLAYFUL),
  GD("MISCHIEVOUS",   "scheming hand-rub, slow sneak turn", G_MISCHIEF),
  GD("CHEEKY",        "cheeky flick up, snap + back, drop", G_CHEEKY),
  GD("DERP",          "floppy async arm flops, uneven", G_DERP),
  GD("LOVING",        "self-hug sweep, turn, hold", G_LOVING),
  GD("TIRED",         "slow arm raise up front, hold, lower", G_TIRED),
  REST("SLEEPING",    "asleep — no body movement (torque off)"),
  GD("YAWNING",       "slow stretch up + out, side turn, hold", G_YAWNING),
  GD("SLEEPY",        "heavy half-lift settling + corrective snap", G_SLEEPY),
  GD("SCARED",        "fast defensive flinch, snap away + tremor", G_SCARED),
  GD("BORED",         "loose hang, occasional single-arm shrugs", G_BORED),
  GD("EMBARRASSED",   "one arm covers face, turn away + tremor", G_EMBARR),
  GD("HUNGRY",        "two 'gimme' reaches toward the food", G_HUNGRY),
};
static const uint8_t SHOW_N = sizeof(SHOW) / sizeof(Gesture);
static uint8_t curIdx = 0;  // which movement is playing; head tap advances it

static void playGesture(uint8_t idx) {  // index, not Gesture&: dodges the .ino auto-prototype ordering bug
  const Gesture& g = SHOW[idx];
  for (uint8_t k = 0; k < g.n; k++) {
    const GStep& s = g.steps[k];
    stepTo(s.armL, s.armR, s.torso, s.ms);
  }
  stepTo(0, 0, 0, RAMP_MS);  // ease down to stop between gestures
}

// Print the movement header + narration once when a new movement is selected.
static void announce(uint8_t idx) {
  const Gesture& g = SHOW[idx];
  Serial.println("\n============================================================");
  Serial.printf ("  MOVEMENT: %s   (%d of %d)\n", g.name, idx + 1, SHOW_N);
  Serial.println("  plays repeatedly until you tap the head sensor:");
  Serial.println(g.desc);
  Serial.println("  returns toward rest each cycle (open-loop net-zero; minor drift possible).");
  Serial.println("============================================================");
}

// Confirmed head-pad read: below threshold twice, to reject servo-PWM spikes.
static bool headTouched() {
  if (touchRead(PIN_TOUCH_HEAD) >= TOUCH_HEAD_THRESH) return false;
  delay(15);
  return touchRead(PIN_TOUCH_HEAD) < TOUCH_HEAD_THRESH;
}

// Smallest runnable check: a gesture that DOESN'T sum to zero drifts by design.
// This catches a mistyped table at boot instead of on the bench after 50 cycles.
static void selfCheck() {
  bool ok = true;
  for (uint8_t g = 0; g < SHOW_N; g++) {
    int32_t sum[3] = { 0, 0, 0 };
    for (uint8_t k = 0; k < SHOW[g].n; k++) {
      const GStep& s = SHOW[g].steps[k];
      sum[0] += (int32_t)s.armL * s.ms;
      sum[1] += (int32_t)s.armR * s.ms;
      sum[2] += (int32_t)s.torso * s.ms;
    }
    for (uint8_t i = 0; i < 3; i++) if (sum[i] != 0) {
      Serial.printf("[FAIL] %s %s not net-zero (sum=%ld) -> will drift\n", SHOW[g].name, AXIS[i], (long)sum[i]);
      ok = false;
    }
  }
  Serial.println(ok ? "[selftest] all gestures net-zero OK" : "[selftest] FIX the tables above");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nBody_Movement_Test — open-loop, net-zero, calibrated");
  for (uint8_t i = 0; i < 3; i++) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(SERVO_PIN[i], 50, 16);
#else
    ledcSetup(i, 50, 16);
    ledcAttachPin(SERVO_PIN[i], i);
#endif
  }
  selfCheck();
  Serial.printf("Servos LIMP for %ds — hand-position the arms to your home now.\n", HOME_SET_MS / 1000);
  limp(HOME_SET_MS);
  stepTo(0, 0, 0, RAMP_MS);  // engage stop-hold at the pose you set
#if CAL_STOP
  Serial.println("CAL_STOP: holding stop — any arm that creeps needs its STOP_TRIM nudged.");
  for (;;) { for (uint8_t i = 0; i < 3; i++) setSpeed(i, 0); delay(250); }
#endif
  Serial.println("Tap the HEAD pad to step to the next movement.");
  announce(curIdx);
}

void loop() {
  playGesture(curIdx);  // one cycle of the current movement

  // Rest ~600ms while watching the head pad; a tap advances to the next one.
  uint32_t t0 = millis();
  bool tapped = false;
  while (millis() - t0 < 600) {
    if (headTouched()) { tapped = true; break; }
    delay(20);
  }
  if (tapped) {
    curIdx = (curIdx + 1) % SHOW_N;
    announce(curIdx);
    while (headTouched()) delay(20);  // wait for finger release, no double-skip
    delay(150);
  }
}
