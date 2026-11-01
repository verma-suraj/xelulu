// MPU6050_Expression_Test.ino — reorient the body, see which expression fires.
//
// Prints the triggered expression whenever the orientation CHANGES, plus the
// shake/knock reactions. No animation, no servos — just the trigger logic and
// a serial print, so you can verify the mapping before wiring it into the bot.
//
// Axis mapping + thresholds match Eilik_Bot (EilikMotion.cpp, Pins.h): robot
// frame is X=forward, Y=left, Z=up, +1g on Z at rest. Wiring: SDA=21, SCL=22,
// MPU6050 at 0x68 (AD0->GND).
//
// The ORIENT_MAP / shake / knock expressions below are just a starting guess —
// edit the strings to whatever emotion you want each pose to trigger.

#include <Wire.h>

#define MPU_ADDR 0x68
#define PIN_SDA  21
#define PIN_SCL  22

// ---- thresholds (from Pins.h) ----
#define DOMINANT_G     0.65f  // a gravity axis must exceed this to name an orientation
#define STEADY_G       0.15f  // |accel-magnitude jump| below this = holding still, trust gravity
#define STABLE_MS      350    // orientation must hold this long before it counts (anti-flicker)
#define SHAKE_SPIKE_G  0.55f  // magnitude jump counted as a shake spike
#define KNOCK_MIN_G    0.20f  // smaller jump = a single knock/poke

// ---- the six orientations, keyed 1:1 to ORIENT_MAP below ----
enum Orient { OR_UPRIGHT, OR_UPSIDE, OR_LEAN_BACK, OR_LEAN_FWD, OR_RIGHT, OR_LEFT, OR_NUM, OR_UNKNOWN };
struct Map { const char* pose; const char* expr; };
static const Map ORIENT_MAP[OR_NUM] = {
  /*OR_UPRIGHT  */ { "UPRIGHT",                        "SMILE" },
  /*OR_UPSIDE   */ { "UPSIDE DOWN",                    "CONFUSED" },
  /*OR_LEAN_BACK*/ { "LEANING BACK (face up)",         "WONDER" },
  /*OR_LEAN_FWD */ { "LEANING FORWARD (face down)",    "SLEEPY" },
  /*OR_RIGHT    */ { "ON ITS RIGHT SIDE",              "SCARED" },
  /*OR_LEFT     */ { "ON ITS LEFT SIDE",               "QUESTIONING" },
};
// motion events (not orientations) — their triggered expressions:
static const char* EXPR_SHAKE = "CONFUSED";
static const char* EXPR_KNOCK = "WONDER";

static unsigned long lastRead = 0;
static float prevMag = 1.0f;
static uint8_t spikeCount = 0;
static unsigned long firstSpike = 0, lastSpike = 0, pendingKnock = 0, shakeCooldownUntil = 0;
static Orient cand = OR_UNKNOWN, shown = OR_UNKNOWN;
static unsigned long candSince = 0;

// Raw MPU read, mapped into the robot frame (X fwd, Y left, Z up). +-4g range.
static bool readAccel(float& ax, float& ay, float& az) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU_ADDR, 6) != 6) return false;
  int16_t x = (Wire.read() << 8) | Wire.read();
  int16_t y = (Wire.read() << 8) | Wire.read();
  int16_t z = (Wire.read() << 8) | Wire.read();
  const float k = 1.0f / 8192.0f;  // +-4g
  ax = -y * k;   // forward
  ay =  x * k;   // left
  az =  z * k;   // up
  return true;
}

// Pure classifier (no I/O) so it can be self-checked. Returns OR_UNKNOWN if no
// single axis clearly points along gravity.
static Orient classifyOrient(float ax, float ay, float az) {
  float axx = fabsf(ax), ayy = fabsf(ay), azz = fabsf(az);
  if (azz >= axx && azz >= ayy) {
    if (azz < DOMINANT_G) return OR_UNKNOWN;
    return az > 0 ? OR_UPRIGHT : OR_UPSIDE;
  }
  if (axx >= ayy) {
    if (axx < DOMINANT_G) return OR_UNKNOWN;
    return ax > 0 ? OR_LEAN_BACK : OR_LEAN_FWD;   // front axis up = face tilted up
  }
  if (ayy < DOMINANT_G) return OR_UNKNOWN;
  return ay > 0 ? OR_RIGHT : OR_LEFT;             // left axis up = lying on right side
}

static void selfCheck() {
  struct { float x, y, z; Orient want; } t[] = {
    {0,0, 1, OR_UPRIGHT}, {0,0,-1, OR_UPSIDE},
    {1,0, 0, OR_LEAN_BACK}, {-1,0,0, OR_LEAN_FWD},
    {0, 1,0, OR_RIGHT}, {0,-1,0, OR_LEFT},
    {0.2f,0.1f,0.3f, OR_UNKNOWN},  // nothing dominant -> unknown
  };
  for (auto& c : t) {
    if (classifyOrient(c.x, c.y, c.z) != c.want) {
      Serial.printf("[FAIL] classify(%.1f,%.1f,%.1f) wrong\n", c.x, c.y, c.z);
    }
  }
  Serial.println("[selftest] orientation classifier checked");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x6B); Wire.write(0x00); Wire.endTransmission();  // wake
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x1C); Wire.write(0x08); Wire.endTransmission();  // +-4g
  Serial.println("\n==============================================");
  Serial.println(" MPU6050 EXPRESSION TRIGGER TEST");
  Serial.println(" Reorient / shake / knock the body — watch below.");
  Serial.println("==============================================");
  selfCheck();
}

void loop() {
  unsigned long now = millis();
  if (now - lastRead < 40) return;   // ~25 Hz
  lastRead = now;

  float ax, ay, az;
  if (!readAccel(ax, ay, az)) return;
  float mag = sqrtf(ax * ax + ay * ay + az * az);
  float delta = fabsf(mag - prevMag);
  prevMag = mag;

  // ---- shake (>=3 spikes / 800ms) vs knock (one spike, then quiet 300ms) ----
  if (delta > SHAKE_SPIKE_G) {
    if (spikeCount == 0 || now - firstSpike > 800) { spikeCount = 0; firstSpike = now; }
    spikeCount++; lastSpike = now; pendingKnock = 0;
    if (spikeCount >= 3 && now > shakeCooldownUntil) {
      spikeCount = 0; shakeCooldownUntil = now + 1500;
      Serial.printf("MOTION: SHAKE            -> Expression: %s\n", EXPR_SHAKE);
    }
  } else if (delta > KNOCK_MIN_G && now - lastSpike > 800 && now > shakeCooldownUntil) {
    if (pendingKnock == 0) pendingKnock = now;
  }
  if (pendingKnock && now - pendingKnock > 300) {
    pendingKnock = 0;
    Serial.printf("MOTION: KNOCK            -> Expression: %s\n", EXPR_KNOCK);
  }

  // ---- orientation: only trust gravity when held reasonably still ----
  if (mag > 0.8f && mag < 1.2f && delta < STEADY_G) {
    Orient o = classifyOrient(ax, ay, az);
    if (o != OR_UNKNOWN) {
      if (o != cand) { cand = o; candSince = now; }        // new candidate, start its timer
      else if (candSince && now - candSince > STABLE_MS && o != shown) {
        shown = o;
        Serial.printf("ORIENTATION: %-28s -> Expression: %s\n", ORIENT_MAP[o].pose, ORIENT_MAP[o].expr);
      }
    }
  }
}
