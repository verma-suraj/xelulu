// MPU6050 shake / knock / tilt detection. Raw Wire reads — no extra library.
#ifndef EILIK_MOTION_H
#define EILIK_MOTION_H

#include <Arduino.h>

enum MotionEvent : uint8_t {
  MOTION_NONE = 0,
  MOTION_KNOCK,   // single bump/vibration (desk knock, poke)
  MOTION_SHAKE,   // repeated strong shaking
  MOTION_TILT,    // picked up / tilted past TILT_DEG
  MOTION_UPRIGHT  // put back level after a TILT
};

bool motionBegin();            // false if MPU6050 not found (bot still runs)
MotionEvent motionUpdate();    // call every loop(); internally rate-limited

#endif
