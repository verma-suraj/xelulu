// Wheels (FW-SOT §7.7): TB6612 at 20 kHz, single-channel encoders on PCNT,
// per-wheel speed PI at 100 Hz, ramps, odometry with a 3 m tether, and the
// interlocks checked every tick. A cliff brakes within one 10 ms tick.
#include "BodyInt.h"
#include "../State.h"
#include "../store/Store.h"
#include "../../Pins.h"
#include "../../Contract.h"
#include "driver/pulse_cnt.h"

static const uint8_t PWM_PIN[2] = {PIN_MOT_L_PWM, PIN_MOT_R_PWM};
static const uint8_t IN1[2] = {PIN_MOT_L_IN1, PIN_MOT_R_IN1};
static const uint8_t IN2[2] = {PIN_MOT_L_IN2, PIN_MOT_R_IN2};
static const uint8_t ENC[2] = {PIN_ENC_L, PIN_ENC_R};

static pcnt_unit_handle_t unit[2];
static int lastCnt[2];
static float speed[2], integ[2], duty[2], travelled[2];
static int8_t sgn[2] = {1, 1};
static uint32_t stallSince[2], deadSince[2], zeroSince;

struct Job {
  bool active, cloud, backoff;
  DriveMode mode;
  uint8_t ticket, phase;
  float v, w, dist, turnDist;
};
static Job job;
static float vr = 0, wr = 0;          // ramped commands: mm/s, rad/s
static float x = 0, y = 0, th = 0, homeX = 0, homeY = 0;
static uint32_t lastActive = 0, startAt = 0, brakeUntil = 0, lastStart = 0;

uint32_t driveLastStartMs() { return lastStart; }

void driveInit() {
  for (uint8_t s = 0; s < 2; s++) {
    pinMode(IN1[s], OUTPUT); pinMode(IN2[s], OUTPUT);
    digitalWrite(IN1[s], LOW); digitalWrite(IN2[s], LOW);
    ledcAttach(PWM_PIN[s], 20000, 10);    // 20 kHz: inaudible
    ledcWrite(PWM_PIN[s], 0);
    pcnt_unit_config_t uc = {};
    uc.low_limit = -10000; uc.high_limit = 10000;
    uc.flags.accum_count = 1;             // the driver extends the 16-bit counter
    pcnt_new_unit(&uc, &unit[s]);
    pcnt_glitch_filter_config_t gf = {.max_glitch_ns = 1000};
    pcnt_unit_set_glitch_filter(unit[s], &gf);
    pcnt_chan_config_t cc = {};
    cc.edge_gpio_num = ENC[s]; cc.level_gpio_num = -1;
    pcnt_channel_handle_t ch;
    pcnt_new_channel(unit[s], &cc, &ch);
    pcnt_channel_set_edge_action(ch, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
    pcnt_unit_add_watch_point(unit[s], 10000);
    pcnt_unit_add_watch_point(unit[s], -10000);
    pcnt_unit_enable(unit[s]);
    pcnt_unit_clear_count(unit[s]);
    pcnt_unit_start(unit[s]);
  }
}

static void motor(uint8_t s, float d) {   // d in -1..1, + = this wheel forward
  d *= cal.wheelDir[s];
  digitalWrite(IN1[s], d > 0);
  digitalWrite(IN2[s], d < 0);
  ledcWrite(PWM_PIN[s], (uint32_t)(min(fabsf(d), 1.0f) * 1023));
}

static void shortBrake(uint32_t now) {    // TB6612 short brake: both IN high; then all pins low
  for (uint8_t s = 0; s < 2; s++) {
    digitalWrite(IN1[s], HIGH); digitalWrite(IN2[s], HIGH);
    ledcWrite(PWM_PIN[s], 1023);
    integ[s] = 0; duty[s] = 0;
  }
  vr = wr = 0;
  brakeUntil = now + 100;
}

static void finish(bool ok, uint32_t now, const char* why) {
  float mm = (travelled[0] + travelled[1]) / 2;
  Serial.printf("[drive] %s %s after %.0f mm%s%s\n", job.backoff ? "back-off" : "move", ok ? "done" : "STOPPED",
                mm, why ? ": " : "", why ? why : "");
  BodyEvent e = {};
  e.kind = BE_DRIVE_DONE; e.ticket = job.ticket; e.ok = ok; e.b = (int16_t)mm; e.ms = now;
  if (!job.backoff) bodyPost(e);
  job.active = false;
  S.driveRunning = false;
  shortBrake(now);
}

static void start(DriveMode m, float v, float w, float dist, bool cloud, uint8_t ticket, bool backoff, uint32_t now) {
  job = {};
  job.active = true; job.cloud = cloud; job.backoff = backoff; job.mode = m; job.ticket = ticket;
  job.v = v; job.w = w; job.dist = dist;
  if (m == DM_COME) {                     // turn toward the last voice first (dir_deg: 0 ahead, CCW +)
    float a = S.lastDirDeg >= 0 ? (S.lastDirDeg <= 180 ? S.lastDirDeg : S.lastDirDeg - 360) : 0;
    job.turnDist = fabsf(a) * DEG_TO_RAD * cal.wheelBaseMm / 2;
    job.w = copysignf(cal.wTurn, a);
    job.phase = fabsf(a) > 10 ? 0 : 1;
    job.dist = min(dist, 2000.0f);        // `come` never drives more than 2 m
  }
  if (m == DM_TURN) job.turnDist = dist;
  travelled[0] = travelled[1] = 0;
  integ[0] = integ[1] = 0;
  if (now - lastActive > 60000) { homeX = x; homeY = y; }   // a new activity starts here (tether home)
  // §2.3: never start the wheels inside 150 ms of a big arm move
  uint32_t sinceArm = now - bodyLastBigArmMs();
  startAt = now + (sinceArm < cal.peakWindowMs ? cal.peakWindowMs - sinceArm : 0);
  lastStart = startAt;
  S.driveRunning = true;
}

void driveCommand(DriveMode m, int16_t vMms, int16_t wMrads, uint16_t distMm, bool cloud, uint8_t ticket, uint32_t now) {
  if (job.active) finish(false, now, "replaced");
  if (m == DM_STOP) {                     // brake now; done once the encoders read zero
    shortBrake(now);
    job = {};
    job.active = true; job.mode = DM_STOP; job.ticket = ticket; job.cloud = cloud;
    zeroSince = 0;
    return;
  }
  float w = wMrads / 1000.0f;
  if (m == DM_TURN && cloud) {            // G4: no direction on the wire -> toward the last voice, else left
    int16_t d = S.lastDirDeg;
    w = (d > 180 && d < 360) ? -fabsf(w) : fabsf(w);
  }
  int8_t dir = m == DM_LINEAR ? (vMms >= 0 ? 1 : -1) : 0;
  bool nothingToDo = (m == DM_TURN && fabsf(w) < 0.05f) || (m != DM_TURN && abs(vMms) < 5) || distMm == 0;
  if (nothingToDo) {                     // zero speed or distance: already there
    BodyEvent e = {};
    e.kind = BE_DRIVE_DONE; e.ticket = ticket; e.ok = true; e.ms = now;
    bodyPost(e);
    return;
  }
  uint8_t il = wheelInterlock(dir, cloud, now);
  if (il) {
    BodyEvent e = {};
    e.kind = BE_DRIVE_DONE; e.ticket = ticket; e.ok = false; e.ms = now;
    bodyPost(e);
    Serial.printf("[drive] refused: %s\n", interlockName(il));
    return;
  }
  start(m, vMms, w, distMm, cloud, ticket, false, now);
}

void driveTick(uint32_t now) {
  const float dt = 0.01f, mmPerCount = PI * cal.wheelDiamMm / cal.countsPerRev, half = cal.wheelBaseMm / 2;
  int cnt[2];
  float d[2];
  for (uint8_t s = 0; s < 2; s++) {
    pcnt_unit_get_count(unit[s], &cnt[s]);
    int c = cnt[s] - lastCnt[s];
    lastCnt[s] = cnt[s];
    d[s] = c * mmPerCount;                // C1 only: no direction, take the commanded one
    speed[s] = sgn[s] * d[s] / dt;
    travelled[s] += d[s];
  }
  float dc = (sgn[0] * d[0] + sgn[1] * d[1]) / 2, dth = (sgn[1] * d[1] - sgn[0] * d[0]) / cal.wheelBaseMm;
  x += dc * cosf(th + dth / 2); y += dc * sinf(th + dth / 2); th += dth;
  S.wheelsMoving = fabsf(speed[0]) > 5 || fabsf(speed[1]) > 5;

  if (brakeUntil) {
    if ((int32_t)(now - brakeUntil) < 0) return;
    for (uint8_t s = 0; s < 2; s++) { digitalWrite(IN1[s], LOW); digitalWrite(IN2[s], LOW); ledcWrite(PWM_PIN[s], 0); }
    brakeUntil = 0;
  }
  S.wheelsAccel = false;
  if (!job.active) return;
  if (job.mode == DM_STOP) {
    if (S.wheelsMoving) zeroSince = 0;
    else if (!zeroSince) zeroSince = now;
    else if (now - zeroSince > 50) finish(true, now, nullptr);
    return;
  }
  if ((int32_t)(now - startAt) < 0) return;
  lastActive = now;

  bool turning = job.mode == DM_TURN || (job.mode == DM_COME && job.phase == 0);
  int8_t dir = turning ? 0 : (job.v >= 0 ? 1 : -1);
  uint8_t il = wheelInterlock(dir, job.cloud, now);
  if (il) {
    bool frontCliff = il == IL_CLIFF_F, cliff = frontCliff || il == IL_CLIFF_R;
    bool wasBackoff = job.backoff;
    finish(false, now, interlockName(il));
    if (cliff) {
      S.edgeUntil = now + 3000;           // AT_EDGE: cloud motion refused for 3 s
      BodyEvent e = {};
      e.kind = BE_CLIFF; e.a = frontCliff ? 0 : 1; e.ms = now;
      bodyPost(e);
      // back off 50 mm, only if the rear cliff AND the rear collision ToF are clear
      if (frontCliff && !wasBackoff && !S.cliffRear && S.tofMm[TOF_REAR] > cal.stopRearMm)
        start(DM_LINEAR, -100, 0, 50, false, 0, true, now + 120);
    }
    return;
  }
  if (dir > 0 && S.tofMm[TOF_FRONT] < cal.stopFrontMm) { finish(job.mode == DM_COME, now, "obstacle ahead"); return; }
  if (dir < 0 && S.tofMm[TOF_REAR] < cal.stopRearMm) { finish(false, now, "obstacle behind"); return; }
  if (job.cloud && hypotf(x - homeX, y - homeY) > cal.tetherMm) { finish(false, now, "3 m tether"); return; }

  // targets, with the speed caps (slower near something ahead)
  float vT = 0, wT = 0, remaining;
  if (turning) {
    remaining = job.turnDist - min(travelled[0], travelled[1]);
    float cap = fabsf(job.w) > cal.wTurn + 0.01f ? cal.wSpin : cal.wTurn;
    wT = constrain(job.w, -cap, cap);
    float wStop = sqrtf(2 * cal.accAng * max(remaining, 0.0f) / half);
    wT = copysignf(min(fabsf(wT), wStop + 0.2f), wT);
  } else {
    remaining = job.dist - (travelled[0] + travelled[1]) / 2;
    float cap = job.v >= 0 ? (S.tofMm[TOF_FRONT] < cal.slowZoneMm ? cal.vSlowMms : cal.vFwdMms) : cal.vRevMms;
    vT = constrain(job.v, -cap, cap);
    float vStop = sqrtf(2 * cal.accLin * 1000 * max(remaining, 0.0f));
    vT = copysignf(min(fabsf(vT), vStop + 15), vT);
  }
  if (remaining <= 0) {
    if (job.mode == DM_COME && job.phase == 0) {
      job.phase = 1;
      travelled[0] = travelled[1] = 0;
      return;
    }
    finish(true, now, nullptr);
    return;
  }

  // ramps (§2.3 soft starts): 0.6 m/s^2, 6 rad/s^2
  float dv = cal.accLin * 1000 * dt, dw = cal.accAng * dt;
  float vPrev = vr, wPrev = wr;
  vr += constrain(vT - vr, -dv, dv);
  wr += constrain(wT - wr, -dw, dw);
  S.wheelsAccel = fabsf(vr) > fabsf(vPrev) + 0.1f || fabsf(wr) > fabsf(wPrev) + 0.001f;

  float target[2] = {vr - wr * half, vr + wr * half};
  for (uint8_t s = 0; s < 2; s++) {
    if (fabsf(target[s]) > 5) sgn[s] = target[s] >= 0 ? 1 : -1;
    float err = target[s] - speed[s];
    integ[s] = constrain(integ[s] + err * dt, -50.0f, 50.0f);
    duty[s] = constrain(target[s] / 320.0f + cal.kp * err + cal.ki * integ[s], -1.0f, 1.0f);
    motor(s, duty[s]);
    // stall: < 10 % of the expected speed for 300 ms at > 50 % duty
    if (fabsf(duty[s]) > 0.5f && fabsf(speed[s]) < 0.1f * fabsf(target[s])) {
      if (!stallSince[s]) stallSince[s] = now;
      else if (now - stallSince[s] > 300) {
        // the other wheel turning normally means the robot moves: this encoder is dead, not stalled
        bool otherOk = fabsf(speed[1 - s]) > 0.3f * fabsf(target[1 - s]);
        Fault f = otherOk ? (s ? F_ENCODER_R_DEAD : F_ENCODER_L_DEAD) : (s ? F_MOTOR_R_STALL : F_MOTOR_L_STALL);
        if (!faultOn(f)) { BodyEvent e = {}; e.kind = BE_FAULT; e.a = f; e.ms = now; bodyPost(e); }
        faultSet(f);
        stallSince[s] = 0;
        finish(false, now, FAULT_NAMES[f]);
        return;
      }
    } else stallSince[s] = 0;
  }
  if (faultOn(F_MOTOR_L_STALL) && fabsf(speed[0]) > 20) faultClear(F_MOTOR_L_STALL);
  if (faultOn(F_MOTOR_R_STALL) && fabsf(speed[1]) > 20) faultClear(F_MOTOR_R_STALL);
}

void driveConsoleStatus() {
  Serial.printf("drive: %s v %.0f mm/s w %.2f rad/s, wheels %.0f/%.0f mm/s, odo x %.0f y %.0f th %.0f deg\n",
                job.active ? "active" : "idle", vr, wr, speed[0], speed[1], x, y, th * RAD_TO_DEG);
}
