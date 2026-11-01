// Body motion for 3x MG90S 360-degree CONTINUOUS-ROTATION servos:
// left arm, right arm, torso. Head servo is NOT installed.
//
// Non-blocking sequencer (call gestureUpdate() every loop). Each emotion is a
// net-zero speed*ms table so the pose returns to where it started; the bench-
// tuned calibration (STOP_TRIM / REV_GAIN / REV_DOWN / SERVO_DIR in the .cpp)
// plus per-step slew smoothing keep drift small and motion un-jerky.
#ifndef EILIK_SERVOS_H
#define EILIK_SERVOS_H

#include <Arduino.h>
#include "EilikFace.h"   // movements are keyed to FaceExpr

void servosBegin();                // attach PWM, all stopped
void gesturePlayFace(FaceExpr f);  // start that emotion's movement
void gestureUpdate();              // call every loop() — non-blocking sequencer
bool gestureBusy();
void servosSleep();                // cut PWM: servos de-energize (Sleeping 32)

#endif
