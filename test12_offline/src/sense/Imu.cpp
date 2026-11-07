// MPU6050 motion (FW-SOT §11.2), the EilikMotion pattern with Xelulu's changes:
// low g is VALID now (it can be dropped), the mount is calibrated once and
// stored (so it may boot while held), and drop / upside-down / spin are new.
// Pickup lives in Tof.cpp: it needs the cliff sensors to confirm it.
#include "SenseBus.h"
#include "../State.h"
#include "../store/Store.h"
#include "../../Contract.h"

#define MPU 0x68
#define G_LSB   8192.0f    // +-4 g
#define DPS_LSB 32.8f      // +-1000 deg/s

volatile bool imuCalReq = false;
static bool present = false;
static float up[3] = {0, 0, 1};
static bool upKnown = false;

// latest processed sample (for the dump)
static float A[3], Gy[3], mag = 1, prevMag = 1;
// one-sample lookahead for the outlier filter
static float hold[6];
static bool haveHold = false, havePrev = false;
static float prevA[3];
static uint32_t rejected = 0, lastValid = 0;

// detectors
static uint8_t spikes = 0;
static uint32_t firstSpike = 0, lastSpike = 0, pendingKnock = 0, shakeCoolUntil = 0;
static float shakePeak = 0;
static uint32_t lastDisturb = 0;
static float peakUp[5] = {0};          // 100 ms buckets over the last 500 ms
static uint32_t bucketAt = 0;
static uint32_t fallStart = 0, fallEnd = 0;
static bool falling = false, watchImpact = false;
static float impactPeak = 0;
static uint32_t upsideSince = 0, tiltSince = 0, tiltClearSince = 0, spinSince = 0, carrySince = 0;
static bool spinning = false, carriedSent = false;

bool imuInit() {
  uint8_t who = 0;
  if (!i2cRead(MPU, 0x75, &who, 1)) { faultSet(F_IMU_MISSING); return false; }
  i2cWrite(MPU, 0x6B, 0x01);   // wake, PLL on the X gyro
  i2cWrite(MPU, 0x19, 9);      // 1 kHz / 10 = 100 Hz
  i2cWrite(MPU, 0x1A, 0x03);   // DLPF ~44 Hz
  i2cWrite(MPU, 0x1B, 0x10);   // +-1000 deg/s
  i2cWrite(MPU, 0x1C, 0x08);   // +-4 g
  present = true;
  if (cal.imuCalDone) {
    memcpy(up, cal.imuUp, sizeof up);
    upKnown = true;
  } else {
    Serial.println("[imu] mount not calibrated: run `cal imu` with the robot upright and still");
  }
  return true;
}

bool imuDisturbedWithin(uint32_t now, uint32_t ms) { return lastDisturb && now - lastDisturb < ms; }

float imuPeakUpG() {
  float p = 0;
  for (float v : peakUp) p = max(p, v);
  return p;
}

static float dot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static void process(const float* a, const float* g, uint32_t now) {
  memcpy(A, a, sizeof A); memcpy(Gy, g, sizeof Gy);
  mag = sqrtf(dot(a, a));
  float gyro = sqrtf(dot(g, g));
  S.imuAt = now;
  lastValid = now;

  // fallback until `cal imu`: learn "up" from the first still second, like Eillik did
  if (!upKnown) {
    static float acc[3]; static int n = 0;
    if (fabsf(mag - 1) < 0.08f && gyro < 5) {
      for (int i = 0; i < 3; i++) acc[i] += a[i] / mag;
      if (++n >= 100) {
        float m = sqrtf(dot(acc, acc));
        for (int i = 0; i < 3; i++) up[i] = acc[i] / m;
        upKnown = true;
        Serial.printf("[imu] up captured at boot (%.2f,%.2f,%.2f) - `cal imu` makes it permanent\n", up[0], up[1], up[2]);
      }
    } else n = 0, acc[0] = acc[1] = acc[2] = 0;
  }

  float aUp = dot(a, up);
  if (now - bucketAt >= 100) {          // rolling 500 ms peak of vertical g (pickup value)
    bucketAt = now;
    memmove(peakUp, peakUp + 1, 4 * sizeof(float));
    peakUp[4] = 0;
  }
  peakUp[4] = max(peakUp[4], aUp);
  if (fabsf(mag - 1) > 0.12f || gyro > 25) lastDisturb = now;

  // tilt from gravity, trusted only when nearly static
  if (upKnown && fabsf(mag - 1) < 0.15f && gyro < 30) {
    float c = constrain(aUp / mag, -1.0f, 1.0f);
    S.tiltDeg = (int16_t)(acosf(c) * 57.2958f);
  }

  bool gate = selfMotionRecently(now);  // the robot shaking itself (Eillik's 81 phantom shakes)
  float delta = fabsf(mag - prevMag);
  prevMag = mag;

  // ---- shake: >= 3 spikes in 800 ms; knock: one spike, then 300 ms quiet ----
  if (gate) { spikes = 0; pendingKnock = 0; }
  else if (delta > cal.shakeG) {
    if (spikes == 0 || now - firstSpike > cal.shakeWinMs) { spikes = 0; firstSpike = now; shakePeak = 0; }
    spikes++;
    shakePeak = max(shakePeak, mag);
    lastSpike = now;
    pendingKnock = 0;
    if (spikes >= 3 && now > shakeCoolUntil) {
      spikes = 0;
      shakeCoolUntil = now + cal.shakeCoolMs;
      post(SE_MOTION, WM_SHAKE, 0, shakePeak);
    }
  } else if (delta > cal.knockG && now - lastSpike > 800 && now > shakeCoolUntil) {
    if (!pendingKnock) pendingKnock = now;
  }
  if (pendingKnock && now - pendingKnock > cal.knockQuietMs) {
    pendingKnock = 0;
    post(SE_KNOCK);
  }

  // ---- drop: free fall is real now. Never gated (safety). ----
  if (mag < cal.dropG) {
    if (!fallStart) fallStart = now;
    if (!falling && now - fallStart >= cal.dropMs) {
      falling = true;
      S.lifted = true;                  // not on the floor, whatever the ToFs say yet
      post(SE_FALLING);
    }
  } else {
    if (falling) {
      uint32_t ms = now - fallStart;
      falling = false;
      fallEnd = now; watchImpact = true; impactPeak = 0;
      post(SE_MOTION, WM_DROP, 0, (float)ms);
    }
    fallStart = 0;
  }
  if (watchImpact) {
    impactPeak = max(impactPeak, mag);
    if (now - fallEnd > 300) {
      watchImpact = false;
      if (impactPeak > cal.impactG) post(SE_IMPACT, 0, 0, impactPeak);
    }
  }

  // ---- upside down > 135 deg for 500 ms, clears below 60 ----
  int16_t tilt = S.tiltDeg;
  if (!S.upsideDown) {
    if (tilt > cal.upsideDeg) {
      if (!upsideSince) upsideSince = now;
      else if (now - upsideSince >= cal.upsideMs) { S.upsideDown = true; post(SE_MOTION, WM_UPSIDE_DOWN, 0, tilt); }
    } else upsideSince = 0;
  } else if (tilt < cal.upsideClearDeg) {
    S.upsideDown = false; upsideSince = 0;
    post(SE_UPRIGHT);
  }

  // ---- tilt 35-135 deg for 400 ms; clears under 20 deg for 600 ms ----
  if (!S.tilted) {
    if (tilt >= cal.tiltDeg && tilt <= cal.upsideDeg) {
      if (!tiltSince) tiltSince = now;
      else if (now - tiltSince >= cal.tiltMs) { S.tilted = true; post(SE_MOTION, WM_TILT, 0, tilt); }
    } else tiltSince = 0;
  } else if (tilt < cal.tiltClearDeg) {
    if (!tiltClearSince) tiltClearSince = now;
    else if (now - tiltClearSince >= cal.tiltClearMs) {
      S.tilted = false; tiltSince = tiltClearSince = 0;
      post(SE_TILT_CLEAR);
    }
  } else tiltClearSince = 0;

  // ---- spin: |yaw rate| > 250 deg/s for 600 ms (local only, G7) ----
  float yaw = fabsf(dot(g, up));
  if (yaw > cal.spinDps) {
    if (!spinSince) spinSince = now;
    else if (!spinning && now - spinSince >= cal.spinMs) { spinning = true; post(SE_SPIN); }
  } else { spinSince = 0; spinning = false; }

  // ---- carried: lifted and moving slowly for > 3 s ----
  if (S.lifted && !falling && gyro > 8 && gyro < 120) {
    if (!carrySince) carrySince = now;
    else if (!carriedSent && now - carrySince > 3000) { carriedSent = true; post(SE_CARRIED); }
  } else if (!S.lifted) { carrySince = 0; carriedSent = false; }
}

void imuStep(uint32_t now) {
  if (!present) return;
  uint8_t b[14];
  if (!i2cRead(MPU, 0x3B, b, 14)) {
    if (now - lastValid > 1000) faultSet(F_IMU_MISSING);
    return;
  }
  float s[6];
  for (int i = 0; i < 3; i++) s[i] = (int16_t)(b[2 * i] << 8 | b[2 * i + 1]) / G_LSB;
  for (int i = 0; i < 3; i++) s[3 + i] = (int16_t)(b[8 + 2 * i] << 8 | b[9 + 2 * i]) / DPS_LSB;
  if (sqrtf(dot(s, s)) > 4.2f) { rejected++; return; }   // saturated garbage; low g is real
  if (faultOn(F_IMU_MISSING)) faultClear(F_IMU_MISSING);

  // a glitched transfer still returns 14 bytes, just wrong ones: drop a sample
  // that disagrees wildly with BOTH neighbours, never move a baseline with it
  if (haveHold) {
    float dPrev = 0, dNext = 0;
    for (int i = 0; i < 3; i++) {
      dPrev += fabsf(hold[i] - prevA[i]);
      dNext += fabsf(hold[i] - s[i]);
    }
    if (havePrev && dPrev > 2.0f && dNext > 2.0f) rejected++;
    else { process(hold, hold + 3, now); memcpy(prevA, hold, sizeof prevA); havePrev = true; }
  }
  memcpy(hold, s, sizeof hold);
  haveHold = true;
}

// `cal imu`: average one still second, store "up" in NVS
bool imuCalStep(uint32_t now) {
  static float acc[3];
  static int n = 0;
  static uint32_t started = 0;
  if (!started) {
    started = now; n = 0; acc[0] = acc[1] = acc[2] = 0;
    Serial.println("[cal imu] hold still, upright...");
  }
  float gyro = sqrtf(dot(Gy, Gy));
  if (fabsf(mag - 1) < 0.08f && gyro < 5) {
    for (int i = 0; i < 3; i++) acc[i] += A[i] / mag;
    n++;
  } else n = 0, acc[0] = acc[1] = acc[2] = 0;
  if (n >= 100) {
    float m = sqrtf(dot(acc, acc));
    for (int i = 0; i < 3; i++) cal.imuUp[i] = up[i] = acc[i] / m;
    cal.imuCalDone = 1;
    upKnown = true;
    calSave();
    Serial.printf("[cal imu] up = (%.3f, %.3f, %.3f) saved\n", up[0], up[1], up[2]);
    started = 0; imuCalReq = false;
  } else if (now - started > 10000) {
    Serial.println("[cal imu] FAILED: never still for 1 s");
    started = 0; imuCalReq = false;
  }
  return imuCalReq;
}

void imuDumpLine() {
  if (!present) { Serial.println("imu,missing"); return; }
  Serial.printf("imu,%lu,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f,mag,%.3f,tilt,%d,rej,%lu\n", millis(), A[0], A[1], A[2],
                Gy[0], Gy[1], Gy[2], mag, S.tiltDeg, (unsigned long)rejected);
}
