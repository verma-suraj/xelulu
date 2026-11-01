// Body motion for 3x MG90S 360° CONTINUOUS-ROTATION servos:
// left arm, right arm, torso. The head servo is NOT installed (the head
// sits loose), so there is no head axis anywhere in this module.
//
// One movement per emotion, encoded from Eilik_Emotion_Movement_Datasheet.md
// (the "Arms" + "Torso" rows), keyed 1:1 to FaceExpr like the sounds are.
//
// IMPORTANT hardware reality: continuous-rotation servos have NO position
// control — only speed + direction. Every datasheet angle is approximated
// by a timed move whose steps sum back to zero net rotation, so poses
// return to neutral by construction. Small drift still accumulates; tune
// SERVO_STOP_TRIM_* in Pins.h first, then step ms/speeds to your linkage.
#ifndef EILIK_SERVOS_H
#define EILIK_SERVOS_H

#include <Arduino.h>
#include "EilikFace.h"   // movements are keyed to FaceExpr

void servosBegin();                // attach PWM, all stopped
void gesturePlayFace(FaceExpr f);  // start that emotion's datasheet movement
void gestureUpdate();              // call every loop() — non-blocking sequencer
void servosUpdate();               // call every loop() — slews speed to cut current spikes
bool gestureBusy();
void servosSleep();                // cut PWM: servos de-energize (Sleeping 32)

#endif
