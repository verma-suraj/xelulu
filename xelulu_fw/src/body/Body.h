// The body task (core 1, priority 21, 100 Hz) owns the servo bus, the motors,
// the encoders and the interlocks (FW-SOT §3.2, §12). No other module writes
// to a servo or a motor driver: everything goes through these queued calls.
#pragma once
#include <Arduino.h>
#include "../../Config.h"
#include "Motions.h"

// §6.2 priority levels: lower number wins; within a level the newest wins.
enum Prio : uint8_t { P_SAFETY, P_PRIVACY, P_VOICE, P_CLOUD, P_LOCAL, P_IDLE };

#define BF_MIRROR 0x01   // left/right swapped, head pan negated
#define BF_HALF   0x02   // half amplitude (low-power mode, §7.5.5)

void bodyBegin();        // servo scan + voltage check + motors safe; starts the task

// ---- motion (§7.3-7.6). ticket != 0: a BE_MOTION_DONE comes back for it ----
void bodyMotion(uint16_t motionId, Prio p, uint8_t flags = 0, uint8_t ticket = 0, int16_t aimDeg = 0);
void bodyDance(uint8_t pattern, uint16_t bpm, uint8_t bars, Prio p, uint8_t ticket = 0);
void bodyClaps(uint8_t n, Prio p);
void bodyPose(int16_t hp, int16_t al, int16_t ar, uint16_t ms, Prio p);
void bodyStop(Prio p);   // end a motion at this level or weaker, ease back to REST
void bodyLimp();         // sleep: all torque off
void bodyWake();         // torque back on where the joints are, then REST
void bodyCompliant(uint8_t joint, uint16_t ms);   // a child holds this arm
void bodyFive();         // high_five: the hand tap arrived
bool bodyBusy(Prio p);   // a motion at level p or stronger is running

// ---- wheels (§7.7) ----
enum DriveMode : uint8_t { DM_LINEAR, DM_TURN, DM_COME, DM_STOP };
// LINEAR: v mm/s (signed) for dist mm · TURN: w mrad/s (signed) until each wheel
// has travelled dist mm · COME: turn toward the last dir_deg, then forward.
void bodyDrive(DriveMode m, int16_t vMms, int16_t wMrads, uint16_t distMm, bool cloud, uint8_t ticket);
void bodyBench(bool on);  // console only: cliff/lift interlocks off for 2 min (wheels in the air)

// ---- events (body -> behaviour) ----
enum BodyEvKind : uint8_t {
  BE_MOTION_DONE,   // ticket, ok, err[] deg, ms, motion
  BE_DRIVE_DONE,    // ticket, ok, b = mm travelled
  BE_CLIFF,         // a = 0 front / 1 rear: braked at an edge
  BE_ARM_GRAB,      // a = joint: a child is holding that arm
  BE_FIVE,          // the high five landed
  BE_FAULT,         // a = Fault
};
struct BodyEvent {
  uint8_t kind, ticket, a;
  bool ok;
  int16_t b;
  uint16_t motion;
  uint32_t ms;
  float err[JOINTS];
};
bool bodyNext(BodyEvent& e);

// ---- console: "servo ...", "rail", "cal servo N center" run inside the body task ----
void bodyConsole(const char* line);
