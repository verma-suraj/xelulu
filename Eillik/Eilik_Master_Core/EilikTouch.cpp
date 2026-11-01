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
    bool touched = touchRead(p.pin) < p.thresh;
    if (touched && !p.down && now - p.lastEvent > 400) {  // 400 ms debounce
      p.down = true;
      p.lastEvent = now;
      return i == 0 ? TOUCH_HEAD_PAT : TOUCH_BODY;
    }
    if (!touched) p.down = false;
  }
  return TOUCH_NONE;
}
