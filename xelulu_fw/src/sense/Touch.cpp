// MPR121 touch (FW-SOT §11.1). The chip's own baseline tracking does the
// drift work Eillik did in software; Eillik's stuck-pad re-learn is kept.
// Classifier: tap 40-350 ms with no second tap inside 400 ms, double_tap =
// 2 taps <= 400 ms apart, hold >= 1 s (fires while still held), pat = 3 taps
// inside 2 s (fires at the 3rd; the rest of the burst extends it).
#include "SenseBus.h"
#include "../State.h"
#include "../store/Store.h"

#define MPR 0x5A
#define STUCK_MS 30000UL

struct ZoneState {
  bool down, holdFired, patActive;
  uint32_t downAt, lastTapEnd, lastPatTap;
  uint8_t pending;                 // taps waiting for the double-tap window to close
  uint32_t taps[3];                // recent tap times (pat detection)
};
static ZoneState zs[ZONES];
static bool present = false;
static uint8_t nEle = 5;

static bool zoneFitted(uint8_t z) { return z < Z_LHAND || cal.handsFitted; }

static bool configure() {
  i2cWrite(MPR, 0x80, 0x63);       // soft reset
  delay(1);
  i2cWrite(MPR, 0x5E, 0x00);       // stop mode: config registers are writable only here
  uint8_t c2 = 0;
  if (!i2cRead(MPR, 0x5D, &c2, 1) || c2 != 0x24) return false;   // CONFIG2 reset value
  nEle = 0;
  for (uint8_t z = 0; z < ZONES; z++) nEle = max<uint8_t>(nEle, cal.touchEle[z] + 1);
  for (uint8_t z = 0; z < ZONES; z++) {
    i2cWrite(MPR, 0x41 + 2 * cal.touchEle[z], cal.touchThr[z]);
    i2cWrite(MPR, 0x42 + 2 * cal.touchEle[z], cal.relThr[z]);
  }
  static const uint8_t FILT[][2] = {   // baseline filter: rising = Adafruit, falling = AN3944's slow one
    // falling was Adafruit's fast 1/5/1/0: the baseline followed a hand coming on through the
    // casing and no touch ever registered (bench 2026-10-31)
    {0x2B, 0x01}, {0x2C, 0x01}, {0x2D, 0x0E}, {0x2E, 0x00}, {0x2F, 0x01}, {0x30, 0x01},
    {0x31, 0xFF}, {0x32, 0x02}, {0x33, 0x00}, {0x34, 0x00}, {0x35, 0x00},
    {0x5B, 0x00},                      // debounce off: the classifier times things
    {0x5C, 0x10}, {0x5D, 0x20},        // 16 uA, 0.5 us, 1 ms period
    {0x7B, 0x0B}, {0x7D, 200}, {0x7E, 130}, {0x7F, 180},   // autoconfig for 3.3 V
  };
  for (auto& r : FILT) i2cWrite(MPR, r[0], r[1]);
  // run: CL = 10 -> baseline tracking on, initial baseline from the first reading
  return i2cWrite(MPR, 0x5E, 0x80 | nEle);
}

bool touchInit() {
  present = configure();
  if (!present) { faultSet(F_TOUCH_MISSING); return false; }
  delay(100);                          // let the baselines settle (boot hands-off, §11.1)
  uint8_t base[13];
  if (i2cRead(MPR, 0x1E, base, nEle)) {
    Serial.print("[touch] baselines:");
    for (uint8_t z = 0; z < ZONES; z++)
      Serial.printf(" %s=%u", zoneName(z), base[cal.touchEle[z]] << 2);
    Serial.printf("  (hands %s)\n", cal.handsFitted ? "fitted" : "not fitted");
  }
  return true;
}

// hand pads ride on the arms: their capacitance changes while the arm moves
static bool gated(uint8_t z, uint32_t now) {
  if (z < Z_LHAND) return false;
  uint8_t arm = z == Z_LHAND ? 0 : 1;
  return S.armMoving[arm] || now - S.armStillSince[arm] < cal.handGateMs;
}

static void tapped(uint8_t z, ZoneState& s, uint32_t now) {
  s.taps[0] = s.taps[1]; s.taps[1] = s.taps[2]; s.taps[2] = now;
  if (s.patActive) { s.lastPatTap = now; return; }     // the burst goes on: extend, no new event
  if (s.taps[0] && now - s.taps[0] <= cal.patWindowMs) {
    post(SE_TOUCH, z, TG_PAT);
    s.patActive = true; s.lastPatTap = now; s.pending = 0;
    return;
  }
  s.pending++;
  s.lastTapEnd = now;
}

void touchStep(uint32_t now) {
  if (!present) return;
  uint8_t st[2];
  if (!i2cRead(MPR, 0x00, st, 2)) return;
  uint16_t bits = st[0] | st[1] << 8;
  for (uint8_t z = 0; z < ZONES; z++) {
    if (!zoneFitted(z)) continue;
    ZoneState& s = zs[z];
    bool on = (bits >> cal.touchEle[z]) & 1;
    if (on && gated(z, now)) on = false;

    if (on && !s.down) {                                   // contact
      s.down = true; s.downAt = now; s.holdFired = false;
      post(SE_TOUCH_DOWN, z);
    } else if (on && s.down) {
      if (!s.holdFired && now - s.downAt >= cal.holdMs) {
        s.holdFired = true; s.pending = 0;
        post(SE_TOUCH, z, TG_HOLD);
      }
      if (now - s.downAt > STUCK_MS) {                     // nobody holds a pad for 30 s
        Serial.printf("[touch] %s stuck: re-learning baselines\n", zoneName(z));
        i2cWrite(MPR, 0x5E, 0x00);
        i2cWrite(MPR, 0x5E, 0x80 | nEle);
        s.down = false;
        post(SE_TOUCH_UP, z, 1);
      }
    } else if (!on && s.down) {                            // release
      s.down = false;
      uint32_t dur = now - s.downAt;
      if (!s.holdFired && dur >= cal.tapMinMs && dur <= cal.tapMaxMs) tapped(z, s, now);
      else if (!s.holdFired) s.pending = 0;                // a long-ish press breaks a tap run
      post(SE_TOUCH_UP, z, s.holdFired);
    }

    // close the double-tap window: 1 tap -> tap, 2 -> double_tap
    if (s.pending && !s.down && now - s.lastTapEnd > cal.doubleGapMs) {
      post(SE_TOUCH, z, s.pending >= 2 ? TG_DOUBLE : TG_TAP);
      s.pending = 0;
    }
    if (s.patActive && now - s.lastPatTap > 1000) {
      s.patActive = false;
      s.taps[0] = s.taps[1] = s.taps[2] = 0;
      post(SE_PAT_END, z);
    }
  }
}

void touchDumpLine() {   // CSV: ms, then filtered/baseline per zone
  if (!present) { Serial.println("touch,missing"); return; }
  uint8_t fd[26], base[13];
  if (!i2cRead(MPR, 0x04, fd, 2 * nEle) || !i2cRead(MPR, 0x1E, base, nEle)) return;
  Serial.printf("touch,%lu", millis());
  for (uint8_t z = 0; z < ZONES; z++) {
    uint8_t e = cal.touchEle[z];
    Serial.printf(",%s,%u,%u", zoneName(z), fd[2 * e] | (fd[2 * e + 1] & 0x03) << 8, base[e] << 2);
  }
  Serial.println();
}
