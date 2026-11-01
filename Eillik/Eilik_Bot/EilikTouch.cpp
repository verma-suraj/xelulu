#include "EilikTouch.h"
#include "Pins.h"

// ESP32 touchRead() drops LOWER when the pad is touched. Thresholds are
// absolute bench-measured values (Pins.h), not baseline-relative.
struct TPad {
  uint8_t pin;
  uint16_t thresh;
  bool down;
  unsigned long lastEvent;
};

static TPad pads[2] = {{PIN_TOUCH_HEAD, TOUCH_HEAD_THRESH, false, 0},
                       {PIN_TOUCH_BODY, TOUCH_BODY_THRESH, false, 0}};

TouchEvent touchPinsUpdate() {
  unsigned long now = millis();
  for (uint8_t i = 0; i < 2; i++) {
    TPad& p = pads[i];
    uint16_t raw = touchRead(p.pin);
    bool touched = raw < p.thresh;
    if (touched && !p.down && now - p.lastEvent > 400) {  // 400 ms debounce
      p.down = true;
      p.lastEvent = now;
      // "print where I have touched" every time a touch is detected
      Serial.printf("TOUCH: %s  (raw=%u  thr=%u)\n",
                    i == 0 ? "HEAD (top pad)" : "BODY (chest pad)", raw, p.thresh);
      return i == 0 ? TOUCH_HEAD_PAT : TOUCH_BODY;
    }
    if (!touched) p.down = false;
  }
  return TOUCH_NONE;
}

// Live raw values for bench calibration — touched reads LOWER than the thresh.
void touchPinsDumpRaw() {
  Serial.printf("[TouchRaw] HEAD pin%u=%u (thr %u)   BODY pin%u=%u (thr %u)\n",
                pads[0].pin, touchRead(pads[0].pin), pads[0].thresh,
                pads[1].pin, touchRead(pads[1].pin), pads[1].thresh);
}
