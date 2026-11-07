// The Sense task owns I2C0 exclusively (FW-SOT §3.3): MPR121 touch,
// MPU6050 IMU, 4x VL53L0X behind the TCA9548A. Core 0, priority 17:
// IMU at 100 Hz, touch and ToF at 50 Hz. Nobody else touches the bus,
// so the Eillik freeze (a Wire call blocked by another task) cannot recur.
#pragma once
#include <Arduino.h>
#include "../../Config.h"

enum TouchGest : uint8_t { TG_TAP, TG_DOUBLE, TG_HOLD, TG_PAT };   // = Contract WireGesture order

enum SenseKind : uint8_t {
  SE_TOUCH_DOWN,   // a zone: contact, right now (the micro-reaction)
  SE_TOUCH,        // a zone, b TouchGest: classified (also goes on the wire)
  SE_TOUCH_UP,     // a zone, b 1 = it was a hold
  SE_PAT_END,      // a zone: a pat burst ended
  SE_MOTION,       // a WireMotion, value in §4.8 units: wire event
  SE_FALLING,      // free fall right now (SCARED during the fall)
  SE_IMPACT,       // value g: landing after a fall
  SE_PUT_DOWN,     // LIFTED cleared
  SE_UPRIGHT,      // upside-down cleared
  SE_TILT_CLEAR,
  SE_SPIN,         // local only (G7)
  SE_KNOCK,        // local only
  SE_CARRIED,      // lifted and moving slowly for > 3 s
  SE_APPROACH,     // a 0 front / 1 rear: something came inside 1 m
};
struct SenseEvent { uint8_t kind, a, b; float value; uint32_t t; };

void senseBegin();
bool senseNext(SenseEvent& e);          // non-blocking, behaviour only

enum SenseDump : uint8_t { DUMP_OFF, DUMP_TOUCH, DUMP_IMU, DUMP_TOF };
void senseDump(SenseDump d);            // CSV at 10 Hz on the console
void senseCalImu();                     // robot upright and still: saves the mount
void senseCalTof();                     // interactive front/rear labelling: saves the map
const char* zoneName(uint8_t z);        // head, back, belly, left_hand, right_hand
