#include "State.h"
#include <atomic>

Shared S = {};

const char* const FAULT_NAMES[F_COUNT] = {
  "imu_missing", "touch_missing", "tof_front_missing", "tof_rear_missing",
  "cliff_front_missing", "cliff_rear_missing",
  "servo_1_missing", "servo_2_missing", "servo_3_missing",
  "servo_1_overvolt", "servo_2_overvolt", "servo_3_overvolt",
  "servo_1_overload", "servo_2_overload", "servo_3_overload",
  "servo_1_overheat", "servo_2_overheat", "servo_3_overheat",
  "motor_l_stall", "motor_r_stall", "encoder_l_dead", "encoder_r_dead",
  "sd_missing", "sd_error", "i2c_bus_stuck", "mic_silent", "rail_low", "brownout_reset",
  "low_power_mode", "proto_mismatch", "cmd_invalid",
};

// Hardware faults that earn one `error` event per boot. The event moves the
// console session to Error, so stalls, power states, a missing SD card and
// the wire faults stay in status.fault[] only.
static const uint32_t ERROR_EVENT_MASK = []() {
  uint32_t m = 0;
  for (int f = F_IMU_MISSING; f <= F_SERVO_OVERHEAT + 2; f++) m |= 1u << f;
  m |= 1u << F_ENCODER_L_DEAD | 1u << F_ENCODER_R_DEAD | 1u << F_SD_ERROR |
       1u << F_I2C_BUS_STUCK | 1u << F_MIC_SILENT;
  return m;
}();

static std::atomic<uint32_t> mask{0}, errorSent{0};
static uint32_t until[F_COUNT];          // timed faults; 0 = not timed

void faultSet(Fault f) {
  uint32_t before = mask.fetch_or(1u << f);
  if (!(before & (1u << f))) Serial.printf("[fault] + %s\n", FAULT_NAMES[f]);
}

void faultClear(Fault f) {
  uint32_t before = mask.fetch_and(~(1u << f));
  until[f] = 0;
  if (before & (1u << f)) Serial.printf("[fault] - %s\n", FAULT_NAMES[f]);
}

bool faultOn(Fault f) { return mask.load() & (1u << f); }
uint32_t faultMask() { return mask.load(); }

void faultFor(Fault f, uint32_t ms) {
  until[f] = millis() + ms;
  if (!until[f]) until[f] = 1;
  faultSet(f);
}

void faultTick(uint32_t now) {
  for (int f = 0; f < F_COUNT; f++)
    if (until[f] && (int32_t)(now - until[f]) >= 0) faultClear((Fault)f);
}

int faultTakeNewError() {
  uint32_t owed = mask.load() & ERROR_EVENT_MASK & ~errorSent.load();
  if (!owed) return -1;
  int f = __builtin_ctz(owed);
  errorSent.fetch_or(1u << f);
  return f;
}

bool selfMotionRecently(uint32_t now) {
  static uint32_t lastMove = 0;
  if (S.servoMoving || S.wheelsMoving) lastMove = now;
  return now - lastMove < 800;
}
