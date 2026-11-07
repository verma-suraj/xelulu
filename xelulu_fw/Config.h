// ============================================================
// Xelulu — compile-time defaults for every [CAL] knob (FW-SOT §18).
// The runtime values live in NVS as ONE versioned blob (store/Store.cpp);
// tune a robot with `cal set <knob> <value>` + `cal save` on the console.
// Change a default here only when it is wrong for every robot.
// Field order = the Report-Back "Hardware facts" order, so `cal show`
// prints the hardware sheet instead of anyone retyping it (§14.4).
// ============================================================
#pragma once
#include <stdint.h>

#define FW_VER       "0.3.0"   // MAJOR.MINOR.PATCH, bump on every build that goes to the matrix (0.3.0: Wi-Fi setup page + OTA over Wi-Fi)
#define FW_SOT_VER   "1.2"     // the FW-SOT revision this firmware implements
#define CAL_VERSION  1         // bump when Cal's layout changes: older blobs load as defaults

// ---- Network: what a freshly flashed robot joins and connects to ----
// Fill these in before flashing, or leave them "" and set them on the robot's own setup page:
// when no Wi-Fi works for 30 s it opens a hotspot named xelulu-xxxxxx (see FLASHING.md).
// A reflash with CHANGED values here overwrites what the robot saved; the same values again
// keep whatever was set later on the console or the setup page.
#define NET_WIFI_SSID     ""                              // 2.4 GHz only
#define NET_WIFI_PASS     ""
#define NET_SERVER        ""                              // the stack PC's LAN IP (ipconfig), e.g. "192.168.1.50"
#define NET_DEVICE_ID     "xlu_000123"                    // = DEVICE_ID of the stack
#define NET_DEVICE_TOKEN  "dev_local_only_not_a_secret"   // = DEVICE_TOKEN of the stack (its dev placeholder)
#define NET_ADMIN_PASS    "xelulu123"                     // setup hotspot, web page (user xelulu), Arduino IDE network upload

// Joint indices (§7.0): head pan, left arm, right arm. SC09 bus ID = index + 1.
enum Joint : uint8_t { HP, AL, AR, JOINTS };

// Touch zones fitted on this build (§2.4). Contract zone strings live in Contract.h.
enum Zone : uint8_t { Z_HEAD, Z_BACK, Z_BELLY, Z_LHAND, Z_RHAND, ZONES };

// ToF roles (§11.3). tofCh[role] = TCA9548A channel.
enum TofRole : uint8_t { TOF_FRONT, TOF_REAR, TOF_CLIFF_F, TOF_CLIFF_R, TOF_ROLES };

struct Cal {
  uint16_t version = CAL_VERSION;

  // ---- Microphones (§5.1) ----
  int8_t   micGainDb = 12;          // SPH0645 is -26 dBFS at 94 dB SPL
  uint8_t  aecRefDelayMs = 0;       // measured once with a click (stage 4)
  uint8_t  micSpacingMm = 0;        // measured on the shell
  uint8_t  micsOnHead = 0;          // 1: add the head-pan angle to dir_deg
  uint8_t  micMsbFormat = 0;        // 1: I2S MSB format (SPH0645 bit-shift quirk)

  // ---- Speaker + amp (§10.4) ----
  int8_t   volMaxDb = 0;            // cap. 0 = full scale = the ceiling (bench 2026-10-31: -6, -3 too quiet); the amp's
                                    // fixed 9 dB on 5 V nears its rails there, so GAIN to GND would only clip
  uint8_t  sndGain = 180;           // Eillik's synth master gain (his 90 was barely audible here). Max 198 = int16 limit
  uint8_t  driveDuckDb = 6;         // extra attenuation while the wheels accelerate (§2.3)

  // ---- Touch: MPR121 (§11.1) ----
  uint8_t  handsFitted = 0;
  uint8_t  touchEle[ZONES] = {0, 1, 2, 3, 4};   // electrode per zone: head, back, belly, L hand, R hand
  uint8_t  touchThr[ZONES] = {21, 24, 12, 6, 6};   // 2026-10-31 hand-through-casing cal: half the touch depth (test3_touch/touch_*.log),
  uint8_t  relThr[ZONES]   = {10, 12, 6, 3, 3};     // belly a third: shallowest pad (34), it lagged at half
  uint16_t tapMinMs = 40;
  uint16_t tapMaxMs = 350;
  uint16_t doubleGapMs = 400;
  uint16_t holdMs = 1000;
  uint16_t patWindowMs = 2000;
  uint16_t handGateMs = 150;        // hand pads ignored while that arm moves + this

  // ---- "LEDs": drawn on screen (§8, §9.1) ----
  uint8_t  glowMode = 0;            // 0 = side bands, 1 = vignette inside the canvas corners
  uint8_t  brightNormal = 100, brightDim = 30, brightQuiet = 15, brightSleep = 5;
  uint8_t  glowCapNormal = 60, glowCapQuiet = 10, glowCapSleep = 5;

  // ---- IMU: GY-87 / MPU6050 (§11.2) ----
  float    imuUp[3] = {0, 0, 1};    // gravity direction when upright (`cal imu`)
  uint8_t  imuCalDone = 0;
  float    shakeG = 0.55f;
  uint16_t shakeWinMs = 800, shakeCoolMs = 1500;
  float    knockG = 0.20f;
  uint16_t knockQuietMs = 300;
  float    dropG = 0.3f;
  uint16_t dropMs = 80;
  float    impactG = 2.0f;
  uint8_t  upsideDeg = 135, upsideClearDeg = 60;
  uint16_t upsideMs = 500;
  uint8_t  tiltDeg = 35, tiltClearDeg = 20;
  uint16_t tiltMs = 400, tiltClearMs = 600;
  uint16_t spinDps = 250, spinMs = 600;

  // ---- Servos: 3x SC09 (§7.1) ----
  uint8_t  jointFitted = 0x07;      // bit per Joint
  uint8_t  servoBusPins = 0;        // 0 auto, 1 = GPIO 43/44, 2 = GPIO 38/47
  int16_t  center[JOINTS] = {202, 512, 512};    // counts at angle 0. Head: straight ahead, measured with test1 2026-11-02
  int8_t   dir[JOINTS] = {1, 1, -1};            // arms are mirror images
  int16_t  softMin[JOINTS] = {-21, -10, -10};   // head: 8 deg inside its end stops (counts 102 / 498), as test12
  int16_t  softMax[JOINTS] = {78, 150, 150};    // ponytail: arms not measured yet (test1), still the -10..150 guess
  int16_t  speedCap[JOINTS] = {300, 400, 400};  // deg/s
  int16_t  accCap[JOINTS] = {2000, 3000, 3000}; // deg/s^2
  uint8_t  torqueNormal[JOINTS] = {50, 50, 50};    // % of stall; written to the servo's max torque
  uint8_t  torqueGentle[JOINTS] = {35, 30, 30};
  uint8_t  torqueCompliant[JOINTS] = {15, 15, 15};
  uint8_t  grabPct = 60;            // arm held by a child
  uint16_t grabMs = 300;
  uint8_t  fiveJumpPct = 30;        // load spike that counts as the high five
  uint8_t  overloadPct = 80;
  uint16_t overloadMs = 1000;
  uint8_t  overheatC = 65;
  uint16_t overvoltMv = 6200;       // SC09 is a 4-6 V part

  // ---- Wheels: 2x N20 + TB6612 (§7.7) ----
  float    wheelDiamMm = 34;
  float    wheelBaseMm = 95;
  uint16_t countsPerRev = 1050;     // 7 x gear ratio (~150:1): MEASURE
  int8_t   wheelDir[2] = {1, -1};   // motor polarity, left / right (mirror-mounted)
  float    kp = 0.004f, ki = 0.02f; // duty per (mm/s) error
  uint16_t vFwdMms = 300, vSlowMms = 150, vRevMms = 100;
  float    wTurn = 2.0f, wSpin = 3.0f;  // rad/s
  float    accLin = 0.6f;           // m/s^2
  float    accAng = 6.0f;           // rad/s^2
  uint16_t tetherMm = 3000;
  uint16_t stopFrontMm = 300, stopRearMm = 150, slowZoneMm = 1000;

  // ---- Battery: rail sag estimate, no voltage pin (§11.5) ----
  uint16_t railOkMv = 5050, railLowMv = 4950, railCritMv = 4800, railEmptyMv = 4650;

  // ---- Load management on the shared rail (§2.3) ----
  uint16_t peakWindowMs = 150;      // no wheel start + big arm move inside this window
  uint8_t  bigArmDeg = 90;

  // ---- ToF: 4x VL53L0X behind a TCA9548A (§11.3) ----
  uint8_t  tofCh[TOF_ROLES] = {0, 1, 2, 3};   // channel per role; boot re-detects the cliff pair
  uint8_t  tofLabelled = 0;         // 1 after `cal tof`
  uint16_t floorMm[2] = {0, 0};     // front / rear cliff floor baseline (0 = capture at boot)
  uint8_t  cliffMarginMm = 25, liftMarginMm = 60;
  uint8_t  sigRateCentiMcps = 25;   // 0.25 MCPS; raise if dark rugs false-stop

  // ---- Sync leads (§5.11) ----
  uint8_t  servoLeadMs = 80, faceLeadMs = 25, glowLeadMs = 10;

  // ---- Voice turn (§5.3-5.6) ----
  uint8_t  opusComplexity = 3, opusKbps = 24;
  uint16_t prerollMs = 300, vadTailMs = 500, pttMaxMs = 15000;
  uint16_t followupMs = 6000, followupGrandmaMs = 8000;
  uint16_t jitterStartMs = 80, jitterMaxMs = 400;
  uint8_t  bargeinMarginDb = 10;    // measured
  float    cryConf = 0.7f;
  uint8_t  cryMinS = 5;

  // ---- Idle life (§6.4) ----
  uint8_t  glanceMinS = 6, glanceMaxS = 12;
  uint8_t  fidgetMinS = 25, fidgetMaxS = 45;
  uint8_t  approachS = 60;
  uint16_t windDownS = 180, windDownQuietS = 30;

  // ---- Clock (G8: quiet hours have no timezone on the wire) ----
  int16_t  tzOffsetMin = 330;       // IST +05:30
};
