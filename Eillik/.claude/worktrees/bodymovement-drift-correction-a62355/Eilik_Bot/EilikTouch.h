// Two ESP32 capacitive touch pins (head pat + body touch).
// Absolute thresholds measured through the printed shell (knobs in Pins.h) —
// no boot calibration, no need to keep hands off at power-on.
#ifndef EILIK_TOUCH_H
#define EILIK_TOUCH_H

#include <Arduino.h>

enum TouchEvent : uint8_t { TOUCH_NONE = 0, TOUCH_HEAD_PAT, TOUCH_BODY };

TouchEvent touchPinsUpdate();  // press-edge events, debounced; prints on each press
void touchPinsDumpRaw();       // print live raw readings of both pads (serial 't')

#endif
