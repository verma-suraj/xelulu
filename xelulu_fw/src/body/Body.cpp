#include "Body.h"
#include "BodyInt.h"
#include "Scs.h"
#include "../State.h"
#include "../store/Store.h"
#include "../face/Face.h"
#include "../audio/Sfx.h"
#include "../../Pins.h"
#include "esp_task_wdt.h"
#include <algorithm>

#define CNT_PER_DEG 3.413f     // SC09: 1024 counts over 300 deg
#define MAX_KF 140

enum CmdType : uint8_t { BC_MOTION, BC_DANCE, BC_CLAPS, BC_POSE, BC_STOP, BC_LIMP, BC_WAKE, BC_COMPLIANT, BC_FIVE, BC_DRIVE };
struct BodyCmd { uint8_t type, prio, flags, ticket; uint16_t id; int16_t a, b, c; uint16_t d; };

static QueueHandle_t cmdQ, evQ, conQ;
static const float REST_POSE[JOINTS] = {0, 10, 10};

struct JointState {
  bool fitted, present, torque, blocked;   // blocked: overvolt / overload / overheat
  float cmd, vel, fb, plan, trem;
  int16_t load, loadPrev;
  uint8_t temp, misses, trips, profile;
  uint16_t mv;
  uint32_t fbAt, loadHighSince, grabSince, grabCooldown, compliantUntil, retryAt, stillSince;
};
static JointState J[JOINTS];
static bool limpAll = true;            // torque stays off until the boot voltage check passes
static int8_t busPair = -1;            // 0 = servos answered on GPIO 20/19, -1 = none (or bus off)
static uint32_t lastBigArm = 0;

// ---- motion player ----
static struct {
  bool active, settling;
  const Kf* kf;
  uint8_t n, prio, ticket, flags, used, waitMask;
  uint16_t dur, id;
  uint32_t start, origin, endAt;
  int16_t aim;
  bool fiveFlag;
  const char* name;
  uint16_t T0[MAX_KF], T1[MAX_KF];
  bool started[MAX_KF];
  float seg[MAX_KF][JOINTS];
} P;
static Kf genBuf[MAX_KF];
static int8_t eyeX = 0, eyeY = 0;

// ===========================================================================
// queued API (any task)
// ===========================================================================
static void send(const BodyCmd& c) { if (cmdQ) xQueueSend(cmdQ, &c, 0); }
void bodyMotion(uint16_t id, Prio p, uint8_t flags, uint8_t ticket, int16_t aim) { send({BC_MOTION, p, flags, ticket, id, aim, 0, 0, 0}); }
void bodyDance(uint8_t pattern, uint16_t bpm, uint8_t bars, Prio p, uint8_t ticket) { send({BC_DANCE, p, 0, ticket, pattern, (int16_t)bpm, bars, 0, 0}); }
void bodyClaps(uint8_t n, Prio p) { send({BC_CLAPS, p, 0, 0, n, 0, 0, 0, 0}); }
void bodyPose(int16_t hp, int16_t al, int16_t ar, uint16_t ms, Prio p) { send({BC_POSE, p, 0, 0, 0, hp, al, ar, ms}); }
void bodyStop(Prio p) { send({BC_STOP, p, 0, 0, 0, 0, 0, 0, 0}); }
void bodyLimp() { send({BC_LIMP, 0, 0, 0, 0, 0, 0, 0, 0}); }
void bodyWake() { send({BC_WAKE, 0, 0, 0, 0, 0, 0, 0, 0}); }
void bodyCompliant(uint8_t j, uint16_t ms) { send({BC_COMPLIANT, 0, 0, 0, j, 0, 0, 0, ms}); }
void bodyFive() { send({BC_FIVE, 0, 0, 0, 0, 0, 0, 0, 0}); }
void bodyDrive(DriveMode m, int16_t v, int16_t w, uint16_t dist, bool cloud, uint8_t ticket) {
  send({BC_DRIVE, (uint8_t)m, (uint8_t)cloud, ticket, 0, v, w, 0, dist});
}
bool bodyBusy(Prio p) { return P.active && P.prio <= p; }
void bodyPost(const BodyEvent& e) { if (evQ) xQueueSend(evQ, &e, 0); }
bool bodyNext(BodyEvent& e) { return evQ && xQueueReceive(evQ, &e, 0) == pdTRUE; }
uint32_t bodyLastBigArmMs() { return lastBigArm; }
void bodyConsole(const char* line) {
  char buf[64];
  strlcpy(buf, line, sizeof buf);
  if (conQ) xQueueSend(conQ, buf, 0);
}

// ===========================================================================
// servos
// ===========================================================================
static bool usable(uint8_t j) { return J[j].fitted && J[j].present && !J[j].blocked; }

static int16_t toCounts(uint8_t j, float deg) {
  return constrain((int)lroundf(cal.center[j] + cal.dir[j] * deg * CNT_PER_DEG), 0, 1023);
}
static float toDeg(uint8_t j, int16_t counts) {
  return (counts - cal.center[j]) / (CNT_PER_DEG * (cal.dir[j] ? cal.dir[j] : 1));
}

static void torque(uint8_t j, bool on) {
  if (!J[j].present) { J[j].torque = false; return; }
  if (on && !J[j].torque) {            // start from where the joint IS, never jump
    J[j].cmd = J[j].plan = J[j].fb;
    J[j].vel = 0;
    uint16_t c = toCounts(j, J[j].cmd);
    uint8_t goal[6] = {(uint8_t)(c >> 8), (uint8_t)c, 0, 0, 0, 0};
    scsWrite(j + 1, SCS_GOAL_POS, goal, 6);
  }
  uint8_t v = on;
  scsWrite(j + 1, SCS_TORQUE_EN, &v, 1);
  J[j].torque = on;
}

static void post(uint8_t kind, uint8_t a, bool ok = true, int16_t b = 0, uint8_t ticket = 0) {
  BodyEvent e = {};
  e.kind = kind; e.a = a; e.ok = ok; e.b = b; e.ticket = ticket; e.ms = millis();
  bodyPost(e);
}

static void jointFault(uint8_t j, Fault f) {
  if (!faultOn(f)) post(BE_FAULT, f);
  faultSet(f);
}

static bool pingAll() {
  scsBegin(PIN_SERVO_TX, PIN_SERVO_RX);
  bool any = false;
  for (uint8_t j = 0; j < JOINTS; j++) if (scsPing(j + 1)) any = true;
  return any;
}

// Ping 1-3 on GPIO 20/19 (§2.5, as built). Those are the native USB pins: with a USB host on the
// port at boot the console wins and the bus stays off, so the servos read missing and gestures
// refuse `unsafe`. Unplug USB and reboot for the servos (no console then).
static void scanBus() {
  busPair = -1;
  if (Serial.isPlugged()) {
    Serial.println("[servo] USB console plugged in: servo bus OFF (it shares GPIO 19/20). Unplug USB + reboot for the servos");
    return;
  }
  if (pingAll()) busPair = 0;
  Serial.printf("[servo] bus on GPIO %d/%d: %s, echo %s\n", PIN_SERVO_TX, PIN_SERVO_RX,
                busPair == 0 ? "servos answer" : "no servo answered", scsEcho() ? "yes (1 kOhm join)" : "no");
}

static void readFeedback(uint8_t j, uint32_t now) {
  ScsFeedback fb;
  if (!scsFeedback(j + 1, fb)) {
    if (J[j].present && ++J[j].misses >= 5) {
      J[j].present = false; J[j].torque = false;
      jointFault(j, (Fault)(F_SERVO_MISSING + j));
      Serial.printf("[servo] %u stopped answering\n", j + 1);
    }
    return;
  }
  if (!J[j].present) {                  // came back
    J[j].present = true;
    faultClear((Fault)(F_SERVO_MISSING + j));
  }
  J[j].misses = 0;
  J[j].fb = toDeg(j, fb.pos);
  J[j].loadPrev = J[j].load;
  J[j].load = fb.load / 10;             // %
  J[j].mv = fb.volt10 * 100;
  J[j].temp = fb.temp;
  J[j].fbAt = now;
  if (!J[j].torque) J[j].cmd = J[j].plan = J[j].fb;   // limp: follow the hand that moves it
}

// overload / overheat / arm grab / the five (§7.1)
static void protect(uint8_t j, uint32_t now) {
  JointState& s = J[j];
  if (!s.present) return;
  int16_t load = abs(s.load);
  if (s.torque && load > cal.overloadPct) {
    if (!s.loadHighSince) s.loadHighSince = now;
    else if (now - s.loadHighSince > cal.overloadMs) {
      torque(j, false); s.blocked = true; s.trips++;
      s.retryAt = s.trips < 3 ? now + 10000 : 0;   // third trip: off until reboot
      jointFault(j, (Fault)(F_SERVO_OVERLOAD + j));
      s.loadHighSince = 0;
    }
  } else s.loadHighSince = 0;
  if (s.temp > cal.overheatC && !s.blocked) {
    torque(j, false); s.blocked = true; s.retryAt = 0;
    jointFault(j, (Fault)(F_SERVO_OVERHEAT + j));
  }
  if (s.blocked && faultOn((Fault)(F_SERVO_OVERHEAT + j)) && s.temp + 10 < cal.overheatC) {
    s.blocked = false; faultClear((Fault)(F_SERVO_OVERHEAT + j));
    if (!limpAll) torque(j, true);
  }
  if (s.blocked && s.retryAt && now > s.retryAt && faultOn((Fault)(F_SERVO_OVERLOAD + j))) {
    s.blocked = false; s.retryAt = 0; faultClear((Fault)(F_SERVO_OVERLOAD + j));
    if (!limpAll) torque(j, true);
  }
  if (j == HP) return;

  // a child holding the arm: load > 60 % for 300 ms while we are not moving it
  bool settled = fabsf(s.plan - s.cmd) < 2 && fabsf(s.vel) < 5 && now - s.stillSince > 300;
  if (s.torque && settled && load > cal.grabPct && now > s.grabCooldown && !(P.waitMask & (1 << j))) {
    if (!s.grabSince) s.grabSince = now;
    else if (now - s.grabSince >= cal.grabMs) {
      post(BE_ARM_GRAB, j);
      s.grabSince = 0; s.grabCooldown = now + 3000;
    }
  } else s.grabSince = 0;
  // high_five holding this arm up: a sudden load jump is the five
  if ((P.waitMask & (1 << j)) && load - abs(s.loadPrev) > cal.fiveJumpPct) P.fiveFlag = true;
}

// ===========================================================================
// motion player
// ===========================================================================
static float ease(uint8_t e, float u) {
  switch (e & 7) {
    case E_EIO: return u < 0.5f ? 4 * u * u * u : 1 - powf(-2 * u + 2, 3) / 2;
    case E_EO:  return 1 - powf(1 - u, 3);
    case E_SPR: { const float c1 = 1.70158f, c3 = c1 + 1; float x = u - 1; return 1 + c3 * x * x * x + c1 * x * x; }  // ~10 % overshoot
    case E_HOLD: return u < 1 ? 0 : 1;
    default: return u;
  }
}

static uint8_t mirrorMask(uint8_t m) { return (m & J_HP) | (m & J_AL ? J_AR : 0) | (m & J_AR ? J_AL : 0); }

static float halfway(uint8_t j, float v) { return (P.flags & BF_HALF) ? REST_POSE[j] + (v - REST_POSE[j]) * 0.5f : v; }

static float mvTarget(const Kf& k, uint8_t j) {
  bool mir = P.flags & BF_MIRROR;
  uint8_t src = mir ? (j == AL ? AR : j == AR ? AL : HP) : j;
  float v = src == HP ? k.a : src == AL ? k.b : k.c;
  if (j == HP && mir) v = -v;
  if ((k.ease & E_AIM) && j == HP) v = constrain(P.aim + k.a, -k.b, k.b);
  return halfway(j, v);
}

// Stretch any keyframe that asks for more than a speed cap; later keyframes
// shift with it, so a gesture never jerks past a cap (§7.0 timing rule).
static void timeWarp() {
  float sim[JOINTS] = {J[HP].plan, J[AL].plan, J[AR].plan};
  for (uint8_t i = 0; i < P.n; i++) {
    const Kf& k = P.kf[i];
    if (k.op != KF_MV) continue;
    uint8_t m = (P.flags & BF_MIRROR) ? mirrorMask(k.mask) : k.mask;
    float dur = P.T1[i] - P.T0[i], need = dur;
    for (uint8_t j = 0; j < JOINTS; j++) {
      if (!(m & (1 << j))) continue;
      float tgt = mvTarget(k, j);
      need = max(need, fabsf(tgt - sim[j]) * 1000.0f / cal.speedCap[j]);
      sim[j] = tgt;
    }
    if (need > dur + 1) {
      uint16_t extra = (uint16_t)(need - dur);
      uint16_t from = P.T0[i];
      P.T1[i] += extra;
      for (uint8_t q = 0; q < P.n; q++)
        if (q != i && P.T0[q] > from) { P.T0[q] += extra; P.T1[q] += extra; }
      P.dur += extra;
    }
  }
}

static void endMotion(bool ok, const float* err) {
  if (P.ticket || P.dur != 0xFFFF) {
    BodyEvent e = {};
    e.kind = BE_MOTION_DONE; e.ticket = P.ticket; e.ok = ok; e.motion = P.id;
    e.ms = millis() - P.origin;
    if (err) memcpy(e.err, err, sizeof e.err);
    bodyPost(e);
  }
  P.active = false;
  for (uint8_t j = 0; j < JOINTS; j++) J[j].profile = TP_NORMAL;
  P.waitMask = 0;
  faceGaze(0, 0); faceEyeTilt(0);
  eyeX = eyeY = 0;
}

static void startKeyframes(const Kf* kf, uint8_t n, uint16_t dur, uint16_t id, const char* name,
                           uint8_t prio, uint8_t ticket, uint8_t flags, int16_t aim, bool warp, uint32_t now) {
  if (P.active) {
    if (prio > P.prio) {                 // weaker than what runs: refused (§6.2)
      if (ticket) { BodyEvent e = {}; e.kind = BE_MOTION_DONE; e.ticket = ticket; e.motion = id; bodyPost(e); }
      return;
    }
    if (P.ticket) endMotion(false, nullptr);   // preempted
  }
  // one arm missing: a one-arm gesture moves to the other arm (§7.1)
  uint8_t used = 0;
  for (uint8_t i = 0; i < n; i++) if (kf[i].op == KF_MV || kf[i].op == KF_OSC || kf[i].op == KF_TRM) used |= kf[i].mask;
  if ((used & J_ARMS) == J_AR && !usable(AR) && usable(AL)) flags ^= BF_MIRROR;
  if ((used & J_ARMS) == J_AL && !usable(AL) && usable(AR)) flags ^= BF_MIRROR;
  if (S.lowPower && prio >= P_LOCAL) flags |= BF_HALF;   // §7.5.5: arms at half amplitude

  P.kf = kf; P.n = n; P.dur = dur; P.id = id; P.name = name;
  P.prio = prio; P.ticket = ticket; P.flags = flags; P.aim = aim;
  P.used = (flags & BF_MIRROR) ? mirrorMask(used) : used;
  P.start = P.origin = now; P.settling = false; P.fiveFlag = false; P.waitMask = 0;
  for (uint8_t i = 0; i < n; i++) { P.T0[i] = kf[i].t0; P.T1[i] = kf[i].t1; P.started[i] = false; }
  for (uint8_t j = 0; j < JOINTS; j++) { J[j].profile = TP_NORMAL; J[j].trem = 0; }
  faceGaze(0, 0); faceEyeTilt(0);
  eyeX = eyeY = 0;
  if (warp && dur != 0xFFFF) timeWarp();
  // §2.3 load management: no big arm move inside 150 ms of a wheel start
  uint32_t sinceWheels = now - driveLastStartMs();
  if (sinceWheels < cal.peakWindowMs) P.start += cal.peakWindowMs - sinceWheels;
  P.active = true;
  Serial.printf("[body] %s (%u kf%s)\n", name, n, flags & BF_MIRROR ? ", mirrored" : "");
}

static void startMotion(uint16_t id, uint8_t prio, uint8_t flags, uint8_t ticket, int16_t aim, uint32_t now) {
  const Motion* m = motionGet(id);
  if (!m) {                               // a face with no motion (SMILE): just settle to REST
    if (prio <= P_LOCAL && (!P.active || prio <= P.prio)) {
      static const Kf REST_KF[] = {{0, 500, KF_MV, J_ALL, E_EIO, 0, 10, 10}};
      startKeyframes(REST_KF, 1, 500, id, "rest", prio, ticket, 0, 0, true, now);
    } else if (ticket) { BodyEvent e = {}; e.kind = BE_MOTION_DONE; e.ticket = ticket; e.motion = id; bodyPost(e); }
    return;
  }
  if (motionMirrored(id)) flags ^= BF_MIRROR;
  startKeyframes(m->kf, m->n, m->durMs, id, m->name, prio, ticket, flags, aim, true, now);
}

static void playerTick(uint32_t now) {
  if (!P.active || (int32_t)(now - P.start) < 0) return;
  uint32_t t = now - P.start;
  bool mir = P.flags & BF_MIRROR;
  for (uint8_t j = 0; j < JOINTS; j++) J[j].trem = 0;
  P.waitMask = 0;

  for (uint8_t i = 0; i < P.n; i++) {
    const Kf& k = P.kf[i];
    uint16_t t0 = P.T0[i], t1 = P.T1[i];
    if (t < t0) continue;
    bool first = !P.started[i];
    P.started[i] = true;
    uint8_t m = mir ? mirrorMask(k.mask) : k.mask;
    float u = t1 > t0 ? min(1.0f, (t - t0) / (float)(t1 - t0)) : 1.0f;

    switch (k.op) {
      case KF_MV:
        for (uint8_t j = 0; j < JOINTS; j++) {
          if (!(m & (1 << j))) continue;
          if (first) {
            P.seg[i][j] = J[j].plan;
            if (j != HP && fabsf(mvTarget(k, j) - J[j].plan) >= cal.bigArmDeg) lastBigArm = now;
          }
          J[j].plan = P.seg[i][j] + (mvTarget(k, j) - P.seg[i][j]) * ease(k.ease, u);
        }
        break;
      case KF_OSC: {
        uint16_t half = k.c ? k.c : 1;
        uint32_t el = min<uint32_t>(t, t1) - t0;
        uint32_t h = el / half;
        float uu = (el % half) / (float)half;
        if (t >= t1) { h = (t1 - t0) / half; h = h ? h - 1 : 0; uu = 1; }
        for (uint8_t j = 0; j < JOINTS; j++) {
          if (!(m & (1 << j))) continue;
          if (first) P.seg[i][j] = J[j].plan;
          float a = k.a, b = k.b;
          if (j == HP && mir) { a = -a; b = -b; }
          if (k.ease & E_REL) { a += P.seg[i][j]; b += P.seg[i][j]; }
          a = halfway(j, a); b = halfway(j, b);
          bool anti = (k.ease & E_ANTI) && j == (mir ? AL : AR);
          auto tgt = [&](uint32_t hh) { return ((hh % 2 == 0) != anti) ? a : b; };
          float from = h == 0 ? P.seg[i][j] : tgt(h - 1);
          J[j].plan = from + (tgt(h) - from) * ease(k.ease, uu);
        }
        break;
      }
      case KF_TRM:
        if (t < t1)
          for (uint8_t j = 0; j < JOINTS; j++)
            if (m & (1 << j)) {
              float amp = k.a * (k.c ? 1 - u : 1) * ((P.flags & BF_HALF) ? 0.5f : 1);
              J[j].trem += amp * sinf(TWO_PI * k.b / 10.0f * (t - t0) / 1000.0f);
            }
        break;
      case KF_EYE:
        if (first) {
          eyeX = mir ? -k.a : k.a; eyeY = k.b;
          faceGaze(eyeX, eyeY);
          faceEyeTilt(mir ? -k.c : k.c);
        }
        break;
      case KF_SND:
        if (first && !S.turnOpen) sfxPlay(k.a);   // no SFX over Xelulu's own voice (§10.2)
        break;
      case KF_TORQ:
        if (!first) break;
        if (k.a == TP_LIMP && m == J_ALL) limpAll = true;   // sleep / updating: stays limp until bodyWake()
        else for (uint8_t j = 0; j < JOINTS; j++) if (m & (1 << j)) J[j].profile = k.a;
        break;
      case KF_WAIT5:
        if (t < t1) {
          P.waitMask = m;
          if (P.fiveFlag) {                 // the five landed: celebrate, same ticket
            post(BE_FIVE, 0);
            uint8_t ticket = P.ticket, prio = P.prio;
            P.ticket = 0;
            P.active = false;
            startMotion(G_CELEBRATE, prio, 0, ticket, 0, now);
            return;
          }
        }
        break;
      case KF_LOOP:                         // jump back to time a: a "while ..." motion
        P.start += t0 - k.a;
        for (uint8_t q = 0; q < P.n; q++) if (P.T0[q] >= k.a) P.started[q] = false;
        return;
    }
  }

  // HP missing: the eyes take the head's look, +-20 px (§7.1)
  if (!usable(HP) && J[HP].fitted) faceGaze((int8_t)constrain(eyeX + J[HP].plan * 20 / 35, -20, 20), eyeY);

  if (P.dur == 0xFFFF || t < P.dur) return;
  // last keyframe reached: done when feedback confirms the pose within +-5 deg, inside 300 ms (§5.11.8)
  if (!P.settling) { P.settling = true; P.endAt = now; }
  float err[JOINTS] = {0, 0, 0};
  bool ok = true;
  for (uint8_t j = 0; j < JOINTS; j++) {
    if (!(P.used & (1 << j)) || !usable(j) || !J[j].torque) continue;
    if (J[j].fbAt < P.endAt) { ok = false; continue; }   // no fresh reading yet
    err[j] = fabsf(J[j].plan - J[j].fb);
    if (err[j] > 5) ok = false;
  }
  if (ok || now - P.endAt >= 300) {
    Serial.printf("[body] %s done %lu ms %s err HP %.1f AL %.1f AR %.1f deg\n", P.name,
                  (unsigned long)(now - P.origin), ok ? "ok" : "FAILED", err[HP], err[AL], err[AR]);
    endMotion(ok, err);
  }
}

// layers -> clamp to soft limits -> rate-limit to the speed and acceleration caps (§5.11.7)
static void servoOut(float dt, uint32_t now) {
  uint8_t ids[JOINTS], data[JOINTS * 6], n = 0;
  bool moving = false;
  for (uint8_t j = 0; j < JOINTS; j++) {
    JointState& s = J[j];
    if (!usable(j)) continue;
    bool limp = limpAll || s.profile == TP_LIMP || now < s.compliantUntil;
    if (limp) { if (s.torque) torque(j, false); continue; }
    if (!s.torque) torque(j, true);
    float target = constrain(s.plan + s.trem, (float)cal.softMin[j], (float)cal.softMax[j]);
    bool gentle = s.profile == TP_GENTLE;
    float vmax = cal.speedCap[j] * (gentle ? 0.7f : 1.0f), amax = cal.accCap[j] * (gentle ? 0.5f : 1.0f);
    float err = target - s.cmd;
    float vdes = copysignf(min(min(vmax, sqrtf(2 * amax * fabsf(err))), fabsf(err) / dt), err);
    float dv = constrain(vdes - s.vel, -amax * dt, amax * dt);
    s.vel += dv;
    s.cmd += s.vel * dt;
    if (fabsf(err) < 0.2f && fabsf(s.vel) < 2) { s.cmd = target; s.vel = 0; }
    if (fabsf(s.vel) > 10) { moving = true; s.stillSince = now; }
    uint16_t c = toCounts(j, s.cmd);
    ids[n] = j + 1;
    uint8_t* d = data + n * 6;
    d[0] = c >> 8; d[1] = c; d[2] = d[3] = d[4] = d[5] = 0;   // time 0, speed 0: track the setpoints
    n++;
  }
  if (n) scsSyncWrite(SCS_GOAL_POS, 6, ids, n, data);
  S.servoMoving = moving;
  for (uint8_t a = 0; a < 2; a++) {
    bool m = fabsf(J[AL + a].vel) > 10;
    if (S.armMoving[a] && !m) S.armStillSince[a] = now;
    S.armMoving[a] = m;
  }
  S.headDeg = (int16_t)J[HP].fb;
}

// rail voltage from the servos, read while they are idle (no load sag), median of 5 (§11.5)
static void railTick(uint32_t now) {
  static uint32_t at = 0, quietSince = 0;
  static uint16_t buf[5];
  static uint8_t n = 0;
  if (S.servoMoving || S.wheelsMoving) { quietSince = now; return; }
  if (now - quietSince < 500 || now - at < 500) return;
  at = now;
  uint16_t mv = 0;
  for (uint8_t j = 0; j < JOINTS; j++) if (J[j].present && now - J[j].fbAt < 500) mv = max(mv, J[j].mv);
  if (!mv) return;
  buf[n++ % 5] = mv;
  uint16_t s[5];
  uint8_t k = min<uint8_t>(n, 5);
  memcpy(s, buf, k * sizeof(uint16_t));
  std::sort(s, s + k);
  S.railMv = s[k / 2];
}

// ===========================================================================
// console (inside the body task: it owns the bus)
// ===========================================================================
static void console(char* line) {
  char* tok = strtok(line, " ");
  if (!tok) return;
  if (!strcmp(tok, "rail")) {
    Serial.printf("rail %u mV, est %u%%, low-power %s, brownout log via `fw`\n", S.railMv, S.batteryPct, S.lowPower ? "ON" : "off");
    for (uint8_t j = 0; j < JOINTS; j++)
      Serial.printf("  servo %u: %s %u mV %u C load %d%%\n", j + 1, J[j].present ? "ok" : "missing", J[j].mv, J[j].temp, J[j].load);
    return;
  }
  if (!strcmp(tok, "cal")) {                // cal servo N center
    strtok(nullptr, " ");
    char* idS = strtok(nullptr, " ");
    int id = idS ? atoi(idS) : 0;
    if (id < 1 || id > 3) { Serial.println("usage: cal servo 1..3 center (torque off, joint at its 0-degree pose)"); return; }
    ScsFeedback fb;
    if (!scsFeedback(id, fb)) { Serial.println("no answer"); return; }
    cal.center[id - 1] = fb.pos;
    J[id - 1].fb = J[id - 1].cmd = J[id - 1].plan = 0;
    Serial.printf("servo %d center = %d counts (run `cal save` to keep it)\n", id, fb.pos);
    return;
  }
  if (strcmp(tok, "servo")) return;
  char* a = strtok(nullptr, " ");
  if (!a) return;
  if (!strcmp(a, "scan")) {   // typed on the USB console, so scanning would take GPIO 19/20 from under it
    Serial.println("servo scan: the bus shares GPIO 19/20 with this USB console. Unplug USB + reboot: the boot scan runs");
    return;
  }
  int id = atoi(a);
  char* op = strtok(nullptr, " ");
  char* arg = strtok(nullptr, " ");
  if (id < 1 || id > 253 || !op) { Serial.println("usage: servo scan | servo ID ping|pos|load|volt|torque on|off|setid NEW|maxtorque PCT"); return; }
  ScsFeedback fb;
  if (!strcmp(op, "ping")) Serial.printf("servo %d %s\n", id, scsPing(id) ? "answers" : "silent");
  else if (!strcmp(op, "pos") || !strcmp(op, "load") || !strcmp(op, "volt")) {
    if (scsFeedback(id, fb)) Serial.printf("servo %d pos %d (%.1f deg) speed %d load %d%% %u mV %u C\n", id, fb.pos,
                                           id <= 3 ? toDeg(id - 1, fb.pos) : 0.0f, fb.speed, fb.load / 10, fb.volt10 * 100, fb.temp);
    else Serial.println("no answer");
  } else if (!strcmp(op, "torque") && arg) {
    bool on = !strcmp(arg, "on");
    if (id <= 3) { if (on) J[id - 1].profile = TP_NORMAL; torque(id - 1, on); if (!on) J[id - 1].profile = TP_LIMP; }
    else { uint8_t v = on; scsWrite(id, SCS_TORQUE_EN, &v, 1); }
    Serial.printf("servo %d torque %s\n", id, on ? "on" : "off");
  } else if (!strcmp(op, "setid") && arg) {
    // factory default is ID 1: assign them one at a time, ALONE on the bus (§2.5)
    Serial.printf("servo %d -> id %d: %s\n", id, atoi(arg), scsSetId(id, atoi(arg)) ? "ok" : "FAILED");
  } else if (!strcmp(op, "maxtorque") && arg) {
    int pct = constrain(atoi(arg), 5, 100);
    Serial.printf("servo %d max torque %d%%: %s\n", id, pct, scsEepromWord(id, SCS_MAX_TORQUE, pct * 10) ? "ok" : "FAILED");
  }
}

// ===========================================================================
// task
// ===========================================================================
static void handle(const BodyCmd& c, uint32_t now) {
  switch (c.type) {
    case BC_MOTION: startMotion(c.id, c.prio, c.flags, c.ticket, c.a, now); break;
    case BC_DANCE: {
      uint8_t n = motionDance(c.id, c.a, c.b, genBuf, MAX_KF);
      static char name[12];
      snprintf(name, sizeof name, "dance_%u", c.id);
      uint16_t dur = genBuf[n - 1].t1;
      startKeyframes(genBuf, n, dur, 0xFFFE, name, c.prio, c.ticket, 0, 0, false, now);   // amplitude, never the beat
      break;
    }
    case BC_CLAPS: {
      const Motion* m = motionGet(MOT_LOCAL(L_CLAPBACK));
      uint8_t n = 0, claps = min<uint8_t>(c.id, 8);
      for (uint8_t i = 0; i < claps; i++)
        for (uint8_t k = 0; k < m->n && n < MAX_KF - 1; k++) {
          genBuf[n] = m->kf[k];
          genBuf[n].t0 += i * 320; genBuf[n].t1 += i * 320;
          n++;
        }
      genBuf[n++] = {(uint16_t)(claps * 320), (uint16_t)(claps * 320 + 300), KF_MV, J_ARMS, E_EIO, 0, 10, 10};
      startKeyframes(genBuf, n, claps * 320 + 300, MOT_LOCAL(L_CLAPBACK), "claps", c.prio, 0, 0, 0, false, now);
      break;
    }
    case BC_POSE: {
      genBuf[0] = {0, c.d, KF_MV, J_ALL, E_EIO, c.a, c.b, c.c};
      startKeyframes(genBuf, 1, c.d, 0xFFFD, "pose", c.prio, 0, 0, 0, true, now);
      break;
    }
    case BC_STOP:
      if (P.active && P.prio >= c.prio) {
        endMotion(false, nullptr);
        static const Kf REST_KF[] = {{0, 600, KF_MV, J_ALL, E_EIO, 0, 10, 10}};
        startKeyframes(REST_KF, 1, 600, 0xFFFC, "rest", P_IDLE, 0, 0, 0, true, now);
      }
      break;
    case BC_LIMP:
      if (P.active) endMotion(false, nullptr);
      limpAll = true;
      Serial.println("[body] limp");
      break;
    case BC_WAKE:
      if (limpAll) {
        limpAll = false;
        for (uint8_t j = 0; j < JOINTS; j++) J[j].plan = J[j].cmd = J[j].fb;
        static const Kf REST_KF[] = {{0, 800, KF_MV, J_ALL, E_EIO, 0, 10, 10}};
        startKeyframes(REST_KF, 1, 800, 0xFFFC, "rest", P_IDLE, 0, 0, 0, true, now);
      }
      break;
    case BC_COMPLIANT:
      if (c.id < JOINTS) {
        J[c.id].compliantUntil = now + c.d;
        J[c.id].profile = TP_GENTLE;      // and come back gently when it ends
      }
      break;
    case BC_FIVE: P.fiveFlag = true; break;
    case BC_DRIVE: driveCommand((DriveMode)c.prio, c.a, c.b, c.d, c.flags, c.ticket, now); break;
  }
}

static void bodyTask(void*) {
  esp_task_wdt_add(nullptr);
  TickType_t last = xTaskGetTickCount();
  uint32_t tick = 0, lastPing = 0;
  uint8_t rr = 0;
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(10));
    esp_task_wdt_reset();
    uint32_t now = millis();
    BodyCmd c;
    while (xQueueReceive(cmdQ, &c, 0) == pdTRUE) handle(c, now);
    char line[64];
    if (xQueueReceive(conQ, line, 0) == pdTRUE) console(line);

    driveTick(now);                       // 100 Hz: the interlocks run every tick

    if (tick % 2 == 0) {                  // 50 Hz trajectory
      playerTick(now);
      servoOut(0.02f, now);
    } else if (tick % 3 == 1) {           // feedback: one servo per read, ~10 Hz each
      for (uint8_t k = 0; k < JOINTS; k++) {
        uint8_t j = (rr + k) % JOINTS;
        if (J[j].fitted && J[j].present) { readFeedback(j, now); protect(j, now); rr = j + 1; break; }
      }
    }
    if (now - lastPing > 2000) {          // look for servos that went missing (or got plugged in)
      lastPing = now;
      for (uint8_t j = 0; j < JOINTS; j++)
        if (J[j].fitted && !J[j].present && scsPing(j + 1)) readFeedback(j, now);
    }
    railTick(now);
    S.cloudBodyBusy = P.active && P.prio == P_CLOUD;
    S.danceRunning = P.active && P.id == 0xFFFE;
    tick++;
  }
}

void bodyBegin() {
  cmdQ = xQueueCreate(16, sizeof(BodyCmd));
  evQ = xQueueCreate(16, sizeof(BodyEvent));
  conQ = xQueueCreate(4, 64);
  driveInit();
  for (uint8_t j = 0; j < JOINTS; j++) {
    J[j] = {};
    J[j].fitted = cal.jointFitted & (1 << j);
    J[j].plan = J[j].cmd = J[j].fb = REST_POSE[j];
  }
  scanBus();

  // §7.1 boot voltage check: anything above 6.2 V keeps its torque off (it is a 4-6 V part)
  for (uint8_t j = 0; j < JOINTS; j++) {
    if (!J[j].fitted) continue;
    ScsFeedback fb;
    J[j].present = scsFeedback(j + 1, fb);
    if (!J[j].present) {
      faultSet((Fault)(F_SERVO_MISSING + j));
      Serial.printf("[servo] %u (%s) MISSING\n", j + 1, j == HP ? "head pan" : j == AL ? "left arm" : "right arm");
      continue;
    }
    J[j].fb = J[j].cmd = J[j].plan = toDeg(j, fb.pos);
    J[j].mv = fb.volt10 * 100;
    J[j].fbAt = millis();
    if (J[j].mv > cal.overvoltMv) {
      J[j].blocked = true;
      faultSet((Fault)(F_SERVO_OVERVOLT + j));
      Serial.printf("\n!!! [servo] %u reads %u mV: ABOVE %u mV. Torque stays OFF. Move its VIN to the 5 V rail !!!\n\n",
                    j + 1, J[j].mv, cal.overvoltMv);
      continue;
    }
    // §2.3 torque cap (halves stall current on the shared rail): EEPROM, written only if it differs
    uint8_t b[2];
    uint16_t want = cal.torqueNormal[j] * 10;
    if (scsRead(j + 1, SCS_MAX_TORQUE, b, 2)) {
      uint16_t have = b[0] << 8 | b[1];
      if (have > 0 && have <= 1000 && have != want) scsEepromWord(j + 1, SCS_MAX_TORQUE, want);
    }
    Serial.printf("[servo] %u ok: %u mV, %.1f deg\n", j + 1, J[j].mv, J[j].fb);
  }
  limpAll = false;   // torque comes on per joint at its current position on the first tick
  xTaskCreatePinnedToCore(bodyTask, "body", 6144, nullptr, 21, nullptr, 1);
}
