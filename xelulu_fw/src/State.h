// Shared robot state and the fault registry.
// Every field of S has ONE writer (named in the comment); everyone else
// only reads. Aligned 8/16/32-bit accesses are atomic on the S3, which is
// all a flag needs; anything bigger moves through a queue instead (§3.3).
#pragma once
#include <Arduino.h>
#include "../Config.h"

// ---- Faults (FW-SOT §4.8), in FAULT_NAMES order ----
enum Fault : uint8_t {
  F_IMU_MISSING, F_TOUCH_MISSING, F_TOF_FRONT_MISSING, F_TOF_REAR_MISSING,
  F_CLIFF_FRONT_MISSING, F_CLIFF_REAR_MISSING,
  F_SERVO_MISSING,                        // + joint
  F_SERVO_OVERVOLT = F_SERVO_MISSING + 3, // + joint
  F_SERVO_OVERLOAD = F_SERVO_OVERVOLT + 3,
  F_SERVO_OVERHEAT = F_SERVO_OVERLOAD + 3,
  F_MOTOR_L_STALL = F_SERVO_OVERHEAT + 3, F_MOTOR_R_STALL, F_ENCODER_L_DEAD, F_ENCODER_R_DEAD,
  F_SD_MISSING, F_SD_ERROR, F_I2C_BUS_STUCK, F_MIC_SILENT, F_RAIL_LOW, F_BROWNOUT_RESET,
  F_LOW_POWER_MODE, F_PROTO_MISMATCH, F_CMD_INVALID,
  F_COUNT
};
static_assert(F_COUNT <= 32, "faults live in one 32-bit mask");
extern const char* const FAULT_NAMES[F_COUNT];

void     faultSet(Fault f);
void     faultClear(Fault f);
bool     faultOn(Fault f);
uint32_t faultMask();
void     faultFor(Fault f, uint32_t ms);   // set now, clear after ms (proto_mismatch, cmd_invalid: 60 s)
void     faultTick(uint32_t now);
int      faultTakeNewError();              // a NEW hardware fault owed its one `error` event this boot, or -1

// ---- Shared state ----
struct Shared {
  // sense task
  volatile bool     lifted, upsideDown, tilted;
  volatile int16_t  tiltDeg;
  volatile uint16_t tofMm[TOF_ROLES];   // 0xFFFF = no valid reading
  volatile bool     cliffFront, cliffRear;
  volatile uint32_t imuAt, tofAt;       // millis() of the last valid sample
  // body task
  volatile bool     servoMoving, wheelsMoving, wheelsAccel;
  volatile bool     armMoving[2];       // [0] left, [1] right (hand-pad gate)
  volatile uint32_t armStillSince[2];
  volatile uint32_t edgeUntil;          // AT_EDGE window end
  volatile bool     cloudBodyBusy;      // a cloud gesture or dance owns the body
  volatile bool     danceRunning;
  volatile bool     driveRunning;
  volatile uint16_t railMv;             // servo-reported 5 V rail, median filtered
  volatile int16_t  headDeg;            // head pan feedback
  // housekeeping (Power)
  volatile uint8_t  batteryPct;         // coarse: 80 / 20 / 10 / 5
  volatile bool     lowPower;           // 2 brownouts in 10 min
  // behaviour
  volatile uint8_t  mode;               // Contract Mode
  volatile bool     quiet;              // quiet mode or inside quiet hours
  volatile bool     asleep;
  volatile uint32_t lastChildMs;        // last child interaction (play-in-quiet-hours rule)
  volatile int16_t  lastDirDeg;         // last wake direction, -1 unknown
  // turn task
  volatile bool     uploading;          // mic audio leaving (privacy indicator)
  volatile bool     turnOpen;           // connecting .. draining, or a barge-in
  volatile bool     crying;             // cry episode: >= cryMinS of crying, until 20 s without
  // net
  volatile bool     wifiUp, mqttUp, timeSynced;
  volatile int8_t   rssi;
};
extern Shared S;

// Self-motion gate for the IMU (§11.2): shakes and knocks are ignored
// while the robot moves itself and for 800 ms after.
bool selfMotionRecently(uint32_t now);
