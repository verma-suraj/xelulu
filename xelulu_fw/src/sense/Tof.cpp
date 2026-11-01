// 4x VL53L0X behind the TCA9548A (FW-SOT §11.3): front + rear collision
// (horizontal), front + rear cliff (looking down). Continuous mode, 20 ms
// budget, polled without blocking: a sensor that is not ready is read next time.
// Cliff = floor + 25 mm OR an invalid reading (fail-safe). Lifted = both
// cliffs > floor + 60 mm for 100 ms AND the IMU felt it.
#include "SenseBus.h"
#include "../State.h"
#include "../store/Store.h"
#include "../../Contract.h"
#include <VL53L0X.h>

#define NO_READING 0xFFFF

volatile bool tofCalReq = false;
static VL53L0X dev[4];               // by mux channel
static bool ok[4];
static uint16_t mm[4] = {NO_READING, NO_READING, NO_READING, NO_READING};
static uint8_t status[4];
static uint32_t seen[4];             // last time each channel produced a reading
static uint8_t role[TOF_ROLES];      // role -> channel
static uint32_t liftSince = 0, floorSince = 0;
static bool farFront = false, farRear = false;

static uint16_t floorOf(uint8_t r) { return cal.floorMm[r == TOF_CLIFF_F ? 0 : 1]; }

static bool readCh(uint8_t ch, uint32_t now) {
  if (!ok[ch] || !muxSelect(ch)) return false;
  VL53L0X& d = dev[ch];
  if ((d.readReg(VL53L0X::RESULT_INTERRUPT_STATUS) & 0x07) == 0) return false;   // not ready: next time
  uint16_t r = d.readReg16Bit(VL53L0X::RESULT_RANGE_STATUS + 10);
  uint8_t st = (d.readReg(VL53L0X::RESULT_RANGE_STATUS) >> 3) & 0x0F;
  d.writeReg(VL53L0X::SYSTEM_INTERRUPT_CLEAR, 0x01);
  if (d.last_status != 0) return false;
  status[ch] = st;
  mm[ch] = (st == 11 && r < 8190) ? r : NO_READING;   // 11 = range valid
  seen[ch] = now;
  return true;
}

void tofInit() {
  for (uint8_t ch = 0; ch < 4; ch++) {
    ok[ch] = false;
    if (!muxSelect(ch)) continue;
    dev[ch].setBus(&Wire);
    dev[ch].setTimeout(30);
    if (!dev[ch].init()) {
      Serial.printf("[tof] CH%u not answering at 0x29: check SDA/SCL/VIN, and XSHUT not pulled low\n", ch);
      continue;
    }
    dev[ch].setSignalRateLimit(cal.sigRateCentiMcps / 100.0f);
    dev[ch].setMeasurementTimingBudget(20000);
    dev[ch].startContinuous(0);
    ok[ch] = true;
  }
  memcpy(role, cal.tofCh, sizeof role);

  // On the floor at boot the cliff pair is the two that see the floor close up
  // (§2.2 #8). `cal tof` labelled front/rear once; this only checks the pair.
  delay(120);
  uint32_t now = millis();
  uint16_t first[4];
  for (uint8_t ch = 0; ch < 4; ch++) { readCh(ch, now); first[ch] = mm[ch]; }
  uint8_t shortOnes[4], n = 0;
  for (uint8_t ch = 0; ch < 4; ch++) if (first[ch] < 100) shortOnes[n++] = ch;
  bool labelledPair = (first[role[TOF_CLIFF_F]] < 100) && (first[role[TOF_CLIFF_R]] < 100);
  if (n == 2 && !labelledPair) {
    if (cal.tofLabelled) Serial.println("[tof] WARNING: the labelled cliff pair does not see the floor; using the two that do");
    uint8_t other[2], k = 0;
    for (uint8_t ch = 0; ch < 4; ch++) if (ch != shortOnes[0] && ch != shortOnes[1]) other[k++] = ch;
    role[TOF_CLIFF_F] = shortOnes[0]; role[TOF_CLIFF_R] = shortOnes[1];
    role[TOF_FRONT] = other[0];       role[TOF_REAR] = other[1];
  } else if (n != 2) {
    Serial.printf("[tof] %u sensors see a floor < 100 mm (expected 2): robot not on the floor? using the stored map\n", n);
  }
  // floor baselines: captured now if they look sane, else the NVS ones (§11.3)
  for (uint8_t i = 0; i < 2; i++) {
    uint16_t v = first[role[i ? TOF_CLIFF_R : TOF_CLIFF_F]];
    if (v >= 15 && v < 150) cal.floorMm[i] = v;
    else if (!cal.floorMm[i]) cal.floorMm[i] = 60;
  }
  static const Fault MISSING[TOF_ROLES] = {F_TOF_FRONT_MISSING, F_TOF_REAR_MISSING, F_CLIFF_FRONT_MISSING, F_CLIFF_REAR_MISSING};
  static const char* const RN[TOF_ROLES] = {"front", "rear", "cliff-front", "cliff-rear"};
  Serial.print("[tof]");
  for (uint8_t r = 0; r < TOF_ROLES; r++) {
    Serial.printf(" %s=CH%u(%s)", RN[r], role[r], ok[role[r]] ? "ok" : "MISSING");
    if (!ok[role[r]]) faultSet(MISSING[r]);
  }
  Serial.printf(" floor %u/%u mm%s\n", cal.floorMm[0], cal.floorMm[1], cal.tofLabelled ? "" : " (front/rear unlabelled: run `cal tof`)");
}

void tofStep(uint32_t now, uint8_t chA, uint8_t chB) {
  readCh(chA, now);
  readCh(chB, now);
  for (uint8_t r = 0; r < TOF_ROLES; r++) S.tofMm[r] = mm[role[r]];

  uint8_t cf = role[TOF_CLIFF_F], cr = role[TOF_CLIFF_R];
  // freshness: both cliff sensors produced a reading lately (§11.2 sensors fresh <= 100 ms)
  uint32_t oldest = min(seen[cf], seen[cr]);
  if (oldest) S.tofAt = oldest;

  uint16_t f = mm[cf], b = mm[cr];
  S.cliffFront = f == NO_READING || f > floorOf(TOF_CLIFF_F) + cal.cliffMarginMm;
  S.cliffRear  = b == NO_READING || b > floorOf(TOF_CLIFF_R) + cal.cliffMarginMm;

  // ---- lifted / put down ----
  if (ok[cf] && ok[cr]) {
    bool highF = f == NO_READING || f > floorOf(TOF_CLIFF_F) + cal.liftMarginMm;
    bool highR = b == NO_READING || b > floorOf(TOF_CLIFF_R) + cal.liftMarginMm;
    if (!S.lifted) {
      if (highF && highR) {
        if (!liftSince) liftSince = now;
        else if (now - liftSince >= 100 && imuDisturbedWithin(now, 500)) {
          S.lifted = true;
          post(SE_MOTION, WM_PICKUP, 0, imuPeakUpG());
        }
      } else liftSince = 0;
    } else {
      if (!S.cliffFront && !S.cliffRear && !imuDisturbedWithin(now, 300)) {
        if (!floorSince) floorSince = now;
        else if (now - floorSince >= 300) {
          S.lifted = false; liftSince = floorSince = 0;
          post(SE_PUT_DOWN);
        }
      } else floorSince = 0;
    }
  } else {
    // As built (Xelulu_Wiring.md §8) there is no cliff pair: the IMU alone says lifted / put down.
    // ponytail: a jolt above LIFT_UP_G lifts, 2 s calm and level puts down. A hand held very still
    // reads as put down; fitting the cliff pair is the real fix.
    const float LIFT_UP_G = 1.2f;
    if (!S.lifted) {
      if (imuPeakUpG() > LIFT_UP_G && imuDisturbedWithin(now, 300)) {
        S.lifted = true;
        post(SE_MOTION, WM_PICKUP, 0, imuPeakUpG());
      }
    } else if (!imuDisturbedWithin(now, 2000) && S.tiltDeg < 15) {   // also clears the IMU's free-fall lift
      S.lifted = false;
      post(SE_PUT_DOWN);
    }
  }

  // ---- someone approaching: a collision sensor drops inside 1 m ----
  uint16_t fr = mm[role[TOF_FRONT]], re = mm[role[TOF_REAR]];
  bool nearF = fr != NO_READING && fr < 1000, nearR = re != NO_READING && re < 1000;
  if (nearF && farFront) post(SE_APPROACH, 0);
  if (nearR && farRear) post(SE_APPROACH, 1);
  farFront = !nearF; farRear = !nearR;
}

// `cal tof`: hand in front of the FRONT, then lift the FRONT edge (§11.3)
bool tofCalStep(uint32_t now) {
  static uint8_t step = 0;
  static uint32_t t0 = 0, okSince = 0;
  static uint8_t cand = 0xFF;
  if (step == 0) {
    step = 1; t0 = now; okSince = 0;
    Serial.println("[cal tof] 1/2: hold a hand ~10 cm in front of the robot's FRONT");
  }
  if (now - t0 > 30000) {
    Serial.println("[cal tof] timed out, nothing saved");
    step = 0; tofCalReq = false;
    return false;
  }
  if (step == 1) {
    uint8_t a = role[TOF_FRONT], b = role[TOF_REAR];
    uint8_t c = (mm[a] < 200) ? a : (mm[b] < 200) ? b : 0xFF;
    if (c != 0xFF && c == cand) {
      if (now - okSince > 300) {
        role[TOF_REAR] = c == a ? b : a;
        role[TOF_FRONT] = c;
        step = 2; t0 = now; cand = 0xFF;
        Serial.println("[cal tof] 2/2: lift the robot's FRONT edge ~3 cm, rear on the floor");
      }
    } else { cand = c; okSince = now; }
  } else if (step == 2) {
    uint8_t a = role[TOF_CLIFF_F], b = role[TOF_CLIFF_R];
    uint16_t va = mm[a], vb = mm[b];
    bool upA = va == NO_READING || va > cal.floorMm[0] + 15;
    bool upB = vb == NO_READING || vb > cal.floorMm[1] + 15;
    uint8_t c = (upA && !upB) ? a : (upB && !upA) ? b : 0xFF;
    if (c != 0xFF && c == cand) {
      if (now - okSince > 300) {
        if (c != a) { uint16_t f0 = cal.floorMm[0]; cal.floorMm[0] = cal.floorMm[1]; cal.floorMm[1] = f0; }
        role[TOF_CLIFF_R] = c == a ? b : a;
        role[TOF_CLIFF_F] = c;
        memcpy(cal.tofCh, role, sizeof role);
        cal.tofLabelled = 1;
        calSave();
        Serial.printf("[cal tof] front=CH%u rear=CH%u cliff-front=CH%u cliff-rear=CH%u saved\n",
                      role[TOF_FRONT], role[TOF_REAR], role[TOF_CLIFF_F], role[TOF_CLIFF_R]);
        step = 0; tofCalReq = false;
      }
    } else { cand = c; okSince = now; }
  }
  return tofCalReq;
}

void tofDumpLine() {   // CSV: ms, then mm/status per role (8191 = no reading)
  static const char* const RN[TOF_ROLES] = {"front", "rear", "cliffF", "cliffR"};
  Serial.printf("tof,%lu", millis());
  for (uint8_t r = 0; r < TOF_ROLES; r++)
    Serial.printf(",%s,%u,%u", RN[r], mm[role[r]] == NO_READING ? 8191 : mm[role[r]], status[role[r]]);
  Serial.printf(",lifted,%d,cliffF,%d,cliffR,%d\n", S.lifted, S.cliffFront, S.cliffRear);
}
