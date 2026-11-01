#include "EilikMotion.h"
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
static unsigned long lastRead = 0;
static float prevMag = 1.0f;
static volatile bool i2cBusBusy = false;  // bus coordination flag with CST816S

// shake = >=3 spikes within 800 ms; knock = one spike then quiet for 300 ms
static uint8_t spikeCount = 0;
static unsigned long firstSpike = 0, lastSpike = 0;
static unsigned long pendingKnock = 0;
static unsigned long shakeCooldownUntil = 0;

static bool tilted = false;
static unsigned long tiltSince = 0, uprightSince = 0;

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
  return true;
}

MotionEvent motionUpdate() {
  if (!present) return MOTION_NONE;
  unsigned long now = millis();
  if (now - lastRead < 50) return MOTION_NONE;  // rate-limit to avoid I2C contention with CST816S
  if (i2cBusBusy) return MOTION_NONE;  // skip if CST816S is mid-transaction
  i2cBusBusy = true;
  lastRead = now;

  float ax, ay, az;
  if (!readAccel(ax, ay, az)) { i2cBusBusy = false; return MOTION_NONE; }
  i2cBusBusy = false;  // release bus after MPU read complete
  float mag = sqrtf(ax * ax + ay * ay + az * az);
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
      return MOTION_SHAKE;
    }
  } else if (delta > KNOCK_MIN_G && now - lastSpike > 800 && now > shakeCooldownUntil) {
    if (pendingKnock == 0) pendingKnock = now;
  }
  if (pendingKnock && now - pendingKnock > 300) {  // quiet since — it was just a knock
    pendingKnock = 0;
    return MOTION_KNOCK;
  }

  // --- tilt: angle of gravity vs the bot's upright Z axis ---
  if (mag > 0.5f && delta < 0.15f) {  // only trust orientation when not being flung around
    bool nowTilted = (az / mag) < cosf(TILT_DEG * PI / 180.0f);
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
