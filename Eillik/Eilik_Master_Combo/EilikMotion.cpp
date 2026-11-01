#include "EilikMotion.h"
#include "EilikServos.h"   // gestureBusy() — to ignore the robot shaking itself
#include "Pins.h"
#include <Wire.h>

#define MPU_ADDR 0x68

// MPU bus selection: if Pins.h routes the MPU onto the same pins as the
// CST816S display bus, reuse Wire (already begun by screenTouch.begin());
// otherwise run it as a second bus on Wire1.
#if (PIN_MPU_SDA == PIN_I2C_SDA) && (PIN_MPU_SCL == PIN_I2C_SCL)
  #define MPU_WIRE     Wire
  #define MPU_OWN_BUS  0
#else
  #define MPU_WIRE     Wire1
  #define MPU_OWN_BUS  1
#endif

static bool present = false;
static float prevMag = 1.0f;
static uint32_t badSamples = 0;   // implausible readings rejected (bus glitches)

// The MPU read runs on its OWN task, never in loop().
//
// Why: Wire.requestFrom() on this bus can block forever. Measured on the bench
// (see debug.md): loop() wedged inside requestFrom for 80+ s with no error
// return, no timeout, no watchdog — core 0 kept running the whole time. The
// documented 50 ms Wire timeout does NOT bound it, so a timeout alone is not a
// fix. Isolating the read means a wedged bus costs the IMU only; face, pads,
// servos and sound keep running.
static volatile MotionEvent pendingEvent = MOTION_NONE;

// shake = >=3 spikes within 800 ms; knock = one spike then quiet for 300 ms
static uint8_t spikeCount = 0;
static unsigned long firstSpike = 0, lastSpike = 0;
static unsigned long pendingKnock = 0;
static unsigned long shakeCooldownUntil = 0;

static bool tilted = false;
static unsigned long tiltSince = 0, uprightSince = 0;
// Orientation-agnostic tilt: the MPU is mounted in a "random" direction, so we
// can't assume which axis is up. Instead we capture the gravity direction once
// at boot (bot sitting upright) and measure the ANGLE away from it. Works for
// any mounting without needing the axis map to be right.
static float restX = 0, restY = 0, restZ = 1;
static bool  restCaptured = false;

// Returns acceleration in the ROBOT frame (X fwd, Y left, Z up, +1g on Z at
// rest) — sensor axes are mapped right here per mpu6050_orientation.md §5,
// raw MPU axes never leave this function.
static bool readAccel(float& ax, float& ay, float& az) {
  MPU_WIRE.beginTransmission(MPU_ADDR);
  MPU_WIRE.write(0x3B);
  if (MPU_WIRE.endTransmission(false) != 0) return false;
  if (MPU_WIRE.requestFrom(MPU_ADDR, 6) != 6) return false;
  int16_t x = (MPU_WIRE.read() << 8) | MPU_WIRE.read();
  int16_t y = (MPU_WIRE.read() << 8) | MPU_WIRE.read();
  int16_t z = (MPU_WIRE.read() << 8) | MPU_WIRE.read();
  const float k = 1.0f / 8192.0f;  // +-4g range
  ax = -y * k;   // robotAccelX = -sensor ay  (forward)
  ay =  x * k;   // robotAccelY = +sensor ax  (left)
  az =  z * k;   // robotAccelZ = +sensor az  (up)
  return true;
}

// A wedged I2C slave holds SDA low between clocks, and Wire hangs trying to
// transfer on it (that is the freeze). Checking the lines directly is enough to
// stay safe: a transfer that is never started cannot block.
//
// Deliberately NO active recovery here. Clocking the bus by hand means taking
// the pins off the I2C peripheral and reinstalling the driver
// (Wire.end()/begin()), and doing that at runtime on this shared bus reliably
// panicked the chip — "Cache error / Cache disabled but cached memory region
// accessed" and "Interrupt wdt timeout on CPU0", once per recovery attempt
// across three runs. A paused IMU is much better than a reboot loop: skip the
// read, keep the robot alive, and let the slave release the line on its own.
static bool busIdle() {
  return digitalRead(PIN_MPU_SDA) == HIGH && digitalRead(PIN_MPU_SCL) == HIGH;
}

static MotionEvent motionSample();

// Settle time after the arms/torso stop before IMU events count again.
// 800 ms: the chassis keeps rocking after the servos stop. Tune down if real
// shakes feel sluggish, up if the robot still startles at its own gestures.
#define SELF_MOTION_SETTLE_MS 800

static void motionTask(void*) {
  unsigned long calmAt = 0;
  uint32_t stuckReads = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(50));       // same 50 ms cadence as before
    if (!busIdle()) {                    // bus wedged: skip, never block on it
      if (++stuckReads % 200 == 1)       // ~ every 10 s while it stays stuck
        Serial.printf("[IMU] I2C bus stuck (SDA low) - IMU paused, robot fine "
                      "(x%lu). Check bus pull-ups/wiring, see debug.md\n",
                      (unsigned long)stuckReads);
      continue;
    }
    stuckReads = 0;

    // Three servos slamming the arms around shake the whole robot, and the MPU
    // reads that as a user shaking it: SHAKE -> scared/crying reaction -> more
    // gesturing -> SHAKE again. Measured 81 false shakes in 200 s before this
    // gate. Keep sampling (so prevMag/rest stay current) but drop the events.
    if (gestureBusy()) calmAt = millis() + SELF_MOTION_SETTLE_MS;
    MotionEvent e = motionSample();
    if (millis() < calmAt) {
      // Dropping the event is not enough: spikes logged while the robot moved
      // itself stay in spikeCount and trip a SHAKE the instant the gate opens
      // (measured: every false shake landed right after "[body] done"). Clear
      // the transient state so the settle window starts from zero.
      spikeCount = 0; pendingKnock = 0; firstSpike = lastSpike = 0;
      continue;
    }
    if (e != MOTION_NONE) pendingEvent = e;
  }
}

// Non-blocking: hands loop() whatever the sensor task last saw.
MotionEvent motionUpdate() {
  MotionEvent e = pendingEvent;
  pendingEvent = MOTION_NONE;
  return e;
}

bool motionBegin() {
#if MPU_OWN_BUS
  MPU_WIRE.begin(PIN_MPU_SDA, PIN_MPU_SCL, 100000);  // standard-mode 100 kHz
#endif
  MPU_WIRE.beginTransmission(MPU_ADDR);
  MPU_WIRE.write(0x75);  // WHO_AM_I
  if (MPU_WIRE.endTransmission(false) != 0 || MPU_WIRE.requestFrom(MPU_ADDR, 1) != 1) return false;
  MPU_WIRE.read();
  MPU_WIRE.beginTransmission(MPU_ADDR); MPU_WIRE.write(0x6B); MPU_WIRE.write(0x00); MPU_WIRE.endTransmission();  // wake
  MPU_WIRE.beginTransmission(MPU_ADDR); MPU_WIRE.write(0x1C); MPU_WIRE.write(0x08); MPU_WIRE.endTransmission();  // +-4g
  present = true;
  // core 0, low priority: blocking here costs nothing that the user can see.
  xTaskCreatePinnedToCore(motionTask, "imu", 4096, nullptr, 1, nullptr, 0);
  return true;
}

// One sensor sample + event detection. Runs ONLY on the IMU task — it may block.
static MotionEvent motionSample() {
  if (!present) return MOTION_NONE;
  unsigned long now = millis();

  float ax, ay, az;
  if (!readAccel(ax, ay, az)) return MOTION_NONE;
  float mag = sqrtf(ax * ax + ay * ay + az * az);

  // This I2C bus is electrically marginal (see debug.md: it wedges and needs
  // recovery ~13x in 240 s). A glitched transfer still returns 6 bytes, just
  // garbage ones, and garbage reads as a huge acceleration spike -> phantom
  // SHAKE. At +-4g full scale nothing real can exceed ~4 g, and a desk robot
  // never sees true free fall, so anything outside this band is a bad read.
  // Reject it WITHOUT touching prevMag, or the next good sample sees a false
  // delta against a garbage baseline.
  if (mag < 0.25f || mag > 4.2f) { badSamples++; return MOTION_NONE; }

  float delta = fabsf(mag - prevMag);
  prevMag = mag;

  // --- spikes: shake vs knock ---
  if (delta > SHAKE_SPIKE_G) {
    if (spikeCount == 0 || now - firstSpike > 800) { spikeCount = 0; firstSpike = now; }
    spikeCount++;
    lastSpike = now;
    pendingKnock = 0;  // big hits belong to a shake, not a knock
    if (spikeCount >= 3 && now > shakeCooldownUntil) {
      spikeCount = 0;
      shakeCooldownUntil = now + 1500;
      Serial.printf("[IMU] shake mag=%.2f d=%.2f bad=%lu\n", mag, delta, (unsigned long)badSamples);
      return MOTION_SHAKE;
    }
  } else if (delta > KNOCK_MIN_G && now - lastSpike > 800 && now > shakeCooldownUntil) {
    if (pendingKnock == 0) pendingKnock = now;
  }
  if (pendingKnock && now - pendingKnock > 300) {  // quiet since — it was just a knock
    pendingKnock = 0;
    return MOTION_KNOCK;
  }

  // --- capture the "level" gravity direction once, at boot (bot upright) ---
  if (!restCaptured && mag > 0.6f && mag < 1.4f) {
    restX = ax / mag; restY = ay / mag; restZ = az / mag;
    restCaptured = true;
    Serial.printf("[IMU] level captured (%.2f,%.2f,%.2f) - tilt ready\n", restX, restY, restZ);
  }

  // --- tilt: ANGLE between live gravity and the captured rest direction ---
  if (restCaptured && mag > 0.5f && delta < 0.15f) {  // only trust when not being flung around
    float cosAng = (ax * restX + ay * restY + az * restZ) / mag;  // rest is a unit vector
    bool nowTilted = cosAng < cosf(TILT_DEG * PI / 180.0f);
    if (nowTilted) {
      uprightSince = 0;
      if (tiltSince == 0) tiltSince = now;
      else if (!tilted && now - tiltSince > 400) { tilted = true; return MOTION_TILT; }
    } else {
      tiltSince = 0;
      if (uprightSince == 0) uprightSince = now;
      else if (tilted && now - uprightSince > 600) { tilted = false; return MOTION_UPRIGHT; }
    }
  }
  return MOTION_NONE;
}
