#include "Safety.h"
#include "BodyInt.h"
#include "../State.h"
#include "../store/Store.h"
#include "../../Contract.h"

volatile uint32_t benchUntil = 0;

// faults that make any wheel motion unsafe (a stall is transient: it does not latch)
static const uint32_t WHEEL_BLOCKING =
  1u << F_IMU_MISSING | 1u << F_TOF_FRONT_MISSING | 1u << F_TOF_REAR_MISSING |
  1u << F_CLIFF_FRONT_MISSING | 1u << F_CLIFF_REAR_MISSING | 1u << F_I2C_BUS_STUCK |
  1u << F_ENCODER_L_DEAD | 1u << F_ENCODER_R_DEAD;

static bool stale(uint32_t now) { return now - S.imuAt > 100 || now - S.tofAt > 100; }

// All must pass, in the travel direction (§7.7). dir: +1 forward, -1 reverse, 0 in place.
uint8_t wheelInterlock(int8_t dir, bool cloud, uint32_t now) {
  bool bench = (int32_t)(benchUntil - now) > 0;
  if (!bench) {
    if (S.lifted) return IL_LIFTED;
    if (dir >= 0 && S.cliffFront) return IL_CLIFF_F;    // in-place turns need both cliffs on the floor
    if (dir <= 0 && S.cliffRear) return IL_CLIFF_R;
  }
  if (S.quiet) return IL_QUIET;
  if (!settings.movementOn) return IL_MOVEMENT_OFF;
  if (S.batteryPct <= 10) return IL_LOW_BATTERY;
  if (S.lowPower) return IL_LOW_POWER;
  if (faultMask() & WHEEL_BLOCKING) return IL_FAULT;
  if (!bench && (S.tiltDeg >= 15 || stale(now))) return S.tiltDeg >= 15 ? IL_TILT : IL_STALE;
  if (cloud && !S.mqttUp) return IL_LINK;
  return IL_OK;
}

const char* interlockName(uint8_t il) {
  static const char* const N[] = {"ok", "lifted", "cliff_front", "cliff_rear", "quiet", "movement_off",
                                  "low_battery", "low_power", "fault", "tilted", "sensor_stale", "link_down"};
  return il < sizeof N / sizeof N[0] ? N[il] : "?";
}

bool wheelsAllowed() { return wheelInterlock(0, false, millis()) == IL_OK; }

// Bench only: wheels in the air read as "lifted" and "cliff". Expires on its own.
void bodyBench(bool on) {
  benchUntil = on ? millis() + 120000 : 0;
  Serial.printf("[drive] bench mode %s\n", on ? "ON for 2 min: cliff + lift interlocks OFF" : "off");
}

static bool noServoUsable() {
  for (uint8_t j = 0; j < 3; j++)
    if (!faultOn((Fault)(F_SERVO_MISSING + j)) && !faultOn((Fault)(F_SERVO_OVERVOLT + j)) &&
        !faultOn((Fault)(F_SERVO_OVERLOAD + j)) && !faultOn((Fault)(F_SERVO_OVERHEAT + j)))
      return false;
  return true;
}

uint8_t refuseGesture() {
  uint32_t now = millis();
  if (S.lifted) return R_PICKED_UP;
  if ((int32_t)(S.edgeUntil - now) > 0) return R_AT_EDGE;   // within 3 s of a cliff
  if (S.quiet) return R_QUIET_HOURS;
  if (S.batteryPct <= 10) return R_LOW_BATTERY;
  if (S.cloudBodyBusy || S.danceRunning) return R_BUSY;
  if (S.lowPower || noServoUsable()) return R_UNSAFE;
  return R_NONE;
}

uint8_t refuseMove(bool isStop) {
  if (isStop) return R_NONE;            // `stop` is always accepted
  uint32_t now = millis();
  if (S.lifted) return R_PICKED_UP;
  if ((int32_t)(S.edgeUntil - now) > 0) return R_AT_EDGE;
  if (S.quiet) return R_QUIET_HOURS;
  if (S.batteryPct <= 10) return R_LOW_BATTERY;
  // movement_on=false has no reason of its own on the wire: unsafe (G9)
  if (!settings.movementOn || S.lowPower || S.tiltDeg >= 15 || stale(now) || !S.mqttUp ||
      (faultMask() & WHEEL_BLOCKING))
    return R_UNSAFE;
  if (S.danceRunning) return R_BUSY;
  return R_NONE;
}

uint8_t refusePlay() {
  if (S.quiet && millis() - S.lastChildMs > 120000) return R_QUIET_HOURS;   // unless a child just played
  if (S.uploading) return R_BUSY;
  return R_NONE;
}

uint8_t refuseSay() {
  if (S.uploading) return R_BUSY;
  if (S.quiet) return R_QUIET_HOURS;
  return R_NONE;
}

uint8_t refuseOta() {
  if (S.batteryPct < 80) return R_LOW_BATTERY;   // the rail has started to sag
  if (S.turnOpen || S.driveRunning || S.danceRunning) return R_BUSY;
  return R_NONE;
}
