// Internal to sense/: bus helpers and the event queue, shared by Touch/Imu/Tof.
// Every function here runs on the Sense task only.
#pragma once
#include "Sense.h"
#include <Wire.h>

bool i2cWrite(uint8_t addr, uint8_t reg, uint8_t val);
bool i2cRead(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t n);
bool muxSelect(uint8_t ch);
void post(uint8_t kind, uint8_t a = 0, uint8_t b = 0, float value = 0);
extern volatile SenseDump dumpMode;

bool touchInit();
void touchStep(uint32_t now);
void touchDumpLine();

bool imuInit();
void imuStep(uint32_t now);
void imuDumpLine();
bool imuDisturbedWithin(uint32_t now, uint32_t ms);
float imuPeakUpG();                     // peak vertical g over the last 500 ms
bool imuCalStep(uint32_t now);          // true while `cal imu` runs
extern volatile bool imuCalReq;

void tofInit();
void tofStep(uint32_t now, uint8_t chA, uint8_t chB);
void tofDumpLine();
bool tofCalStep(uint32_t now);          // true while `cal tof` runs
extern volatile bool tofCalReq;
