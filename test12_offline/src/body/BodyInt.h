// Internal to body/: what Body.cpp, Drive.cpp and Safety.cpp share. Body task only.
#pragma once
#include "Body.h"

void     bodyPost(const BodyEvent& e);
uint32_t bodyLastBigArmMs();      // load management: no wheel start inside 150 ms of a big arm move

void driveInit();
void driveCommand(DriveMode m, int16_t vMms, int16_t wMrads, uint16_t distMm, bool cloud, uint8_t ticket, uint32_t now);
void driveTick(uint32_t now);     // 100 Hz: encoders, interlocks, ramps, PI, odometry
uint32_t driveLastStartMs();
void driveConsoleStatus();

// Interlocks for wheel motion in a travel direction (+1 forward, -1 reverse, 0 in place).
enum Interlock : uint8_t {
  IL_OK, IL_LIFTED, IL_CLIFF_F, IL_CLIFF_R, IL_QUIET, IL_MOVEMENT_OFF, IL_LOW_BATTERY,
  IL_LOW_POWER, IL_FAULT, IL_TILT, IL_STALE, IL_LINK,
};
uint8_t wheelInterlock(int8_t dir, bool cloud, uint32_t now);
const char* interlockName(uint8_t il);
extern volatile uint32_t benchUntil;
