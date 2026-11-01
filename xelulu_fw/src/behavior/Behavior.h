// The behaviour engine (FW-SOT §6, §7.5, §4.7). Runs in loopTask on core 1,
// once per frame: drains the sense, body and command queues, runs the
// reaction tables, idle life and modes, then renders the face.
#pragma once
#include <Arduino.h>
#include "../face/Face.h"

void behaviorBegin();                 // after every module: the boot sequence (§7.5.5)
void behaviorLoop(uint32_t now);      // one `now` per loop, passed down (Eillik's underflow lesson)

// console hooks
void behaviorFace(FaceId f);          // `face N`: the full reaction (face, motion, sound, glow)
void behaviorGesture(uint8_t g);      // `gesture name`
void behaviorDance(uint8_t pattern, uint16_t bpm);
void behaviorSetMode(uint8_t mode);   // `mode idle|play|story|quiet|sleep`
void behaviorPrintState(Print& out);
