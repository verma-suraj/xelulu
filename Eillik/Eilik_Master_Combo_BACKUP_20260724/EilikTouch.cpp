#include "EilikTouch.h"
#include "Pins.h"

// ESP32 touchRead() drops LOWER when the pad is touched.
//
// Absolute thresholds do not survive contact with reality. Bench-measured on
// this build (see debug.md): the body pad reads 1331..1387 untouched against a
// 1350 threshold — it sits ON the trip point and fires by itself (3 phantom
// touches in a 200 s idle run). The head pad reads ~1385 against a 950
// threshold, needing a 32% drop that a normal finger never produces, so it
// barely triggers at all. Both numbers also drift with humidity, temperature
// and how close the servo wiring runs.
//
// So: learn each pad's own untouched baseline at boot and trigger on a
// RELATIVE drop, then let the baseline creep to follow slow drift. The
// thresholds in Pins.h are kept only as a sanity clamp.
#define DEBOUNCE_MS   60
#define TOUCH_DROP    0.86f   // touched when reading < 86% of baseline
#define RELEASE_DROP  0.92f   // hysteresis: must come back above 92% to release
#define BASELINE_N    32      // samples averaged at boot
#define DRIFT_SHIFT   9       // baseline creep rate when untouched (1/512 per read)
// Nobody holds a pad for half a minute. If one reads "pressed" that long, the
// boot baseline no longer matches reality (robot moved, cable now resting on
// the pad, humidity shift) and drift tracking cannot recover it — the creep
// only runs while untouched, so a latched pad would stay latched forever.
// That is the exact failure the old fixed thresholds had, so re-learn instead.
#define STUCK_MS      30000UL

struct TPad {
  uint8_t  pin;
  uint32_t baseline;
  bool     down;
  unsigned long since;       // when this pad went down
  unsigned long lastChange;  // debounce timestamp
};

static TPad pads[2] = {
  { PIN_TOUCH_HEAD, 0, false, 0, 0 },
  { PIN_TOUCH_BODY, 0, false, 0, 0 },
};

void touchBegin() {
  for (uint8_t i = 0; i < 2; i++) {
    uint32_t sum = 0;
    for (uint8_t n = 0; n < BASELINE_N; n++) { sum += touchRead(pads[i].pin); delay(2); }
    pads[i].baseline = sum / BASELINE_N;
    Serial.printf("[touch] GPIO%u baseline=%lu -> press below %lu\n",
                  pads[i].pin, (unsigned long)pads[i].baseline,
                  (unsigned long)(pads[i].baseline * TOUCH_DROP));
  }
}

TouchEx touchUpdate() {
  unsigned long now = millis();
  TouchEx ev = { 0, 0, 0, { 0, 0 } };
  const uint8_t bit[2] = { PAD_HEAD, PAD_BODY };

  for (uint8_t i = 0; i < 2; i++) {
    TPad& p = pads[i];
    if (!p.baseline) continue;              // touchBegin() not run yet
    uint32_t v = touchRead(p.pin);

    bool touched = p.down ? (v < p.baseline * RELEASE_DROP)
                          : (v < p.baseline * TOUCH_DROP);

    // Track slow drift only while clearly untouched, so a resting hand can
    // never train the baseline down onto itself.
    if (!touched && v > p.baseline * RELEASE_DROP)
      p.baseline += ((int32_t)v - (int32_t)p.baseline) >> DRIFT_SHIFT;

    if (touched && p.down && now - p.since > STUCK_MS) {
      p.baseline = v;                     // adopt the new resting level
      p.down = false; touched = false;
      Serial.printf("[touch] GPIO%u stuck - baseline re-learned = %lu\n",
                    p.pin, (unsigned long)v);
    }

    if (touched != p.down && now - p.lastChange > DEBOUNCE_MS) {
      p.lastChange = now;
      if (touched) { p.down = true;  p.since = now;      ev.pressed  |= bit[i]; }
      else         { p.down = false; ev.released |= bit[i];
                     ev.holdMs[i] = (uint16_t)min(now - p.since, 60000UL); }
    }
    if (p.down) ev.held |= bit[i];
  }
  return ev;
}
