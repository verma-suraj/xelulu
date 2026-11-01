#include "EilikTouch.h"
#include "Pins.h"

// ESP32 touchRead() drops LOWER when the pad is touched. Absolute bench-measured
// thresholds (Pins.h). Edge-detected with a short debounce so hold timing and
// simultaneity stay responsive enough for combo detection.
#define DEBOUNCE_MS 60

struct TPad {
  uint8_t  pin;
  uint16_t thresh;
  bool     down;
  unsigned long since;       // when this pad went down
  unsigned long lastChange;  // debounce timestamp
};

static TPad pads[2] = {
  { PIN_TOUCH_HEAD, TOUCH_HEAD_THRESH, false, 0, 0 },
  { PIN_TOUCH_BODY, TOUCH_BODY_THRESH, false, 0, 0 },
};

TouchEx touchUpdate() {
  unsigned long now = millis();
  TouchEx ev = { 0, 0, 0, { 0, 0 } };
  const uint8_t bit[2] = { PAD_HEAD, PAD_BODY };

  for (uint8_t i = 0; i < 2; i++) {
    TPad& p = pads[i];
    bool touched = touchRead(p.pin) < p.thresh;
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
