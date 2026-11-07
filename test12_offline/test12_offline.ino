// ============================================================
// TEST 12 - OFFLINE: every feature that works without Wi-Fi, run by xelulu_fw's own code.
//   servos (head sweep, arms), N20 wheels (slow, fenced), speaker, body movement (21 gestures,
//   48 expressions with their motion + sound, 10 dances), touch, IMU, and VOICE COMMANDS
//   recognised on the ESP32-S3 itself, with what it heard written on the screen.
// Checklist to tick against: "Xelulu Offline Test Checklist" (claude.ai doc). T1..T11 = TEST_NAMES order.
//
// CONTROLS
//   menu:    HEAD tap = next test · BACK tap = previous · BELLY tap = run it
//   BACK touch = emergency stop (wheels, motion, sound) and aborts a test, except in T10 (the pad test)
//   say "stop" = the same, by voice
//   voice, in the menu: forward · backward · left · right · stop · hello · dance (variants: PHRASES[])
//   lift the robot and set it down: that spot becomes the fence's centre
// NO ToF in this test: obstacles and table edges are NOT seen. The wheels move at DRIVE_MMS, one short
// step per word, and refuse a step that would end more than FENCE_MM from the centre.
//
// USB (as built the servo bus is on the native USB pins 19/20; same rule as xelulu_fw):
//   USB plugged in at boot -> console on COM10, servos OFF (servo tests run, nothing moves)
//   USB unplugged at boot  -> servos ON, no console: the screen is the output
//   Reflash after a servos-on boot: hold BOOT while plugging USB in.
//   Console (USB boots only): `t N` run test N · `say forward` = as if heard · `stop` · `home` · `?`
//
// CODE: xelulu_fw's modules copied UNCHANGED into src/ on 2026-11-02 (re-copy when they change):
//   face/ Face Glow · audio/ Sfx ClipsData · body/ Body Drive Safety Scs Motions · sense/ Sense Imu Touch
//   State · store/Store.h · Pins.h Config.h Contract.h build_opt.h
// This file adds: the ToF stub, the speaker task (test8's), the mics + ESP-SR, the tests, the screen text.
// Voice = ESP-SR from core 3.3.10: AFE + MultiNet7 English on a FIXED phrase list (max 300), no wake word.
// It can't transcribe free speech: no model that does fits an S3 (test10 does that on the PC).
//
// BUILD: arduino-cli, core esp32 3.3.10, TFT_eSPI 2.5.43 (build_opt.h here; build --clean after editing it)
//   FQBN esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,CDCOnBoot=cdc,USBMode=hwcdc,FlashMode=qio,PartitionScheme=esp_sr_16
//   The upload also writes srmodels.bin (3.3 MB) at 0xC10000, over xelulu_fw's unused "content" area.
//   NVS (0x9000) is the same in both layouts. Flashing xelulu_fw afterwards restores its own table.
// ============================================================
#include <TFT_eSPI.h>
#include "driver/i2s_std.h"
#include "esp_partition.h"
#include "esp32-hal-sr.h"
#include "Pins.h"
#include "Contract.h"
#include "src/State.h"
#include "src/store/Store.h"
#include "src/face/Face.h"
#include "src/audio/Sfx.h"
#include "src/body/BodyInt.h"
#include "src/sense/SenseBus.h"

// ---- test knobs (RAM only: nothing is written to NVS) ----
#define HEAD_MID_DEG   -90.8f   // test1 reading (2026-11-02): head looking straight ahead
#define HEAD_STOP_A   -120.1f   // test1 reading at one end stop
#define HEAD_STOP_B     -4.1f   // test1 reading at the other end stop
#define HEAD_MARGIN      8.0f   // the software limit stays this far inside each end stop
#define HEAD_DIR         1      // -1 if T4's "left limit" turns the head to the robot's RIGHT
#define DRIVE_MMS       60      // wheel speed, mm/s (xelulu_fw: 300 forward, 100 reverse)
#define TURN_RADS     0.8f      // in-place turn, rad/s (xelulu_fw: 2.0)
#define STEP_MM         60      // one "forward" / "backward"
#define TURN_DEG        45      // one "left" / "right"
#define FENCE_MM       250      // no step may end further than this from the centre
#define MIC_SHIFT       14      // 32-bit mic word >> 14 = xelulu_fw's +12 dB. 13 if far words are missed, 15 if close ones clip
#define VOLUME         100      // 0-100 on xelulu_fw's curve: -40 + 0.4 x level dB (100 = full scale)
#define MUTE_TAIL_MS   150      // the mics are fed silence while the speaker plays, + this (no echo canceller)
#define GAP_MS         400      // pause between the steps of a test
#define RATE         16000
#define BLOCK          160      // speaker block, 10 ms

Cal cal;                         // Config.h defaults + applyTestCal(); xelulu_fw's NVS blob is not read
Settings settings = {};
void calSave() { Serial.println("[test12] cal is not saved by this sketch"); }   // only `cal imu` calls it

// ============================================================
// ToF stub. ponytail: no ToF in this test (user, 2026-11-02): "nothing ahead", no cliff, fresh data.
// Lift / put-down = the IMU-only branch of xelulu_fw's Tof.cpp (this build has no cliff pair either).
// ============================================================
volatile bool tofCalReq = false;
void tofInit() { Serial.println("[tof] not used by test12: obstacles and edges are NOT seen"); }
void tofDumpLine() {}
bool tofCalStep(uint32_t) { tofCalReq = false; return false; }
void tofStep(uint32_t now, uint8_t, uint8_t) {
  for (auto& v : S.tofMm) v = 0xFFFF;
  S.cliffFront = false;
  S.cliffRear = false;
  S.tofAt = now;
  if (!S.lifted) {
    if (imuPeakUpG() > 1.2f && imuDisturbedWithin(now, 300)) {
      S.lifted = true;
      post(SE_MOTION, WM_PICKUP, 0, imuPeakUpG());
    }
  } else if (!imuDisturbedWithin(now, 2000) && S.tiltDeg < 15) {
    S.lifted = false;
    post(SE_PUT_DOWN);
  }
}

static void applyTestCal() {
  // head: test1 shows deg = (counts - 512) x 300/1024. Centre = straight ahead, limits inside both stops
  float lo = min(HEAD_STOP_A, HEAD_STOP_B), hi = max(HEAD_STOP_A, HEAD_STOP_B);
  float a = HEAD_DIR * (lo + HEAD_MARGIN - HEAD_MID_DEG), b = HEAD_DIR * (hi - HEAD_MARGIN - HEAD_MID_DEG);
  cal.center[HP] = (int16_t)lroundf(512 + HEAD_MID_DEG * 1024 / 300);
  cal.dir[HP] = HEAD_DIR;
  cal.softMin[HP] = (int16_t)ceilf(min(a, b));    // rounded inward
  cal.softMax[HP] = (int16_t)floorf(max(a, b));
  // ponytail: arms keep Config.h's centre 512 and -10..150 deg until they are measured with test1 too
  cal.vFwdMms = cal.vSlowMms = cal.vRevMms = DRIVE_MMS;
  cal.wTurn = cal.wSpin = TURN_RADS;
  settings.movementOn = true;
  settings.volume = VOLUME;
  S.batteryPct = 80;   // ponytail: no battery reading here (Power.cpp needs the servo rail); assumed fine
}

// ============================================================
// screen: four text lines drawn on the face canvas (the part the 1.5x zoom shows: x 13-226, y 40-199)
// ============================================================
enum Line : uint8_t { L_TITLE, L_INFO, L_HEARD, L_RESULT, LINES };
static const uint8_t LINE_Y[LINES] = {41, 58, 166, 183};
static char lineTxt[LINES][48];
static uint16_t lineCol[LINES];
static volatile float micDbL = -120, micDbR = -120;
static volatile bool micMute = false, srOk = false;

static void lineSet(uint8_t i, uint16_t col, const char* fmt, ...) {   // app task only (and setup, before it)
  char b[sizeof lineTxt[0]];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof b, fmt, ap);
  va_end(ap);
  lineCol[i] = col;
  if (!strcmp(b, lineTxt[i])) return;
  strcpy(lineTxt[i], b);
  if (b[0]) Serial.printf("[screen] %s\n", b);   // the console sees what the screen shows
}

void faceOverlay(TFT_eSprite& s) {   // Face.cpp calls this before every push
  s.setTextDatum(TL_DATUM);
  for (uint8_t i = 0; i < LINES; i++) {
    if (!lineTxt[i][0]) continue;
    s.fillRect(14, LINE_Y[i] - 1, 212, 17, TFT_BLACK);
    s.setTextColor(lineCol[i], TFT_BLACK);
    s.drawString(lineTxt[i], 16, LINE_Y[i], 2);
  }
  // mic level, the louder mic, -80..-20 dBFS; grey while muted for the speaker
  float db = max(micDbL, micDbR);
  int w = constrain((int)((db + 80) * 208 / 60), 0, 208);
  s.fillRect(16, 161, 208, 3, TFT_DARKGREY);
  s.fillRect(16, 161, w, 3, micMute ? TFT_LIGHTGREY : db > -45 ? TFT_GREEN : TFT_DARKGREEN);
}

// ============================================================
// speaker: xelulu_fw Audio.cpp's I2S1 TX, SFX channel only (as test8)
// ============================================================
static i2s_chan_handle_t tx;
static void speakerTask(void*) {
  static int16_t sfx[BLOCK], out[BLOCK * 2];
  const float gain = powf(10.0f, min(-40.0f + 0.4f * VOLUME, (float)cal.volMaxDb) / 20.0f);
  uint32_t lastLoud = 0;
  size_t w;
  for (;;) {
    sfxRender(sfx, BLOCK);
    for (int i = 0; i < BLOCK; i++) out[2 * i] = out[2 * i + 1] = (int16_t)constrain(sfx[i] * gain, -32768.f, 32767.f);
    if (sfxBusy()) lastLoud = millis();
    micMute = millis() - lastLoud < MUTE_TAIL_MS;
    i2s_channel_write(tx, out, sizeof out, &w, portMAX_DELAY);   // paces this task at 10 ms a block
  }
}
static void speakerBegin() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  cc.dma_desc_num = 4;
  cc.dma_frame_num = BLOCK;
  cc.auto_clear = true;
  i2s_new_channel(&cc, &tx, nullptr);
  i2s_std_config_t sc = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {I2S_GPIO_UNUSED, (gpio_num_t)PIN_SPK_BCLK, (gpio_num_t)PIN_SPK_LRC, (gpio_num_t)PIN_SPK_DIN,
                 I2S_GPIO_UNUSED, {false, false, false}},
  };
  i2s_channel_init_std_mode(tx, &sc);
  i2s_channel_enable(tx);
  xTaskCreatePinnedToCore(speakerTask, "speaker", 4096, nullptr, 22, nullptr, 1);
}

// ============================================================
// voice: I2S0 mics (xelulu_fw Audio.cpp's setup) -> L+R average -> ESP-SR, words only, no wake word
// ============================================================
enum Cmd : uint8_t { C_FORWARD, C_BACKWARD, C_LEFT, C_RIGHT, C_STOP, C_HELLO, C_DANCE, C_COUNT };
static const char* const CMD_NAMES[C_COUNT] = {"forward", "backward", "left", "right", "stop", "hello", "dance"};
static const sr_cmd_t PHRASES[] = {   // several phrases may share a command; short single words are the hardest
  {C_FORWARD, "forward"}, {C_FORWARD, "go forward"}, {C_FORWARD, "move forward"},
  {C_BACKWARD, "backward"}, {C_BACKWARD, "go backward"}, {C_BACKWARD, "move backward"}, {C_BACKWARD, "go back"},
  {C_LEFT, "left"}, {C_LEFT, "turn left"}, {C_LEFT, "go left"},
  {C_RIGHT, "right"}, {C_RIGHT, "turn right"}, {C_RIGHT, "go right"},
  {C_STOP, "stop"}, {C_STOP, "stop moving"}, {C_STOP, "stop now"},
  {C_HELLO, "hello"}, {C_HELLO, "hello there"}, {C_HELLO, "hi there"},
  {C_DANCE, "dance"}, {C_DANCE, "lets dance"},
};
static const int N_PHRASES = sizeof PHRASES / sizeof PHRASES[0];
struct Heard { int8_t cmd, phrase; };
static QueueHandle_t heardQ;
static i2s_chan_handle_t rx;

// ESP-SR's feed task (core 0) asks for len bytes of 16-bit mono at 16 kHz
static esp_err_t micFill(void*, void* out, size_t len, size_t* got, uint32_t) {
  static int32_t raw[256 * 2];
  static float dcL, dcR, x1, y1;
  int16_t* o = (int16_t*)out;
  size_t want = len / 2, n = 0;
  float sl = 0, sr = 0;
  while (n < want) {
    size_t br = 0, fr = min(want - n, (size_t)256);
    i2s_channel_read(rx, raw, fr * 8, &br, 200);
    if (!br) break;
    for (size_t i = 0; i < br / 8; i++, n++) {
      float l = raw[2 * i] >> MIC_SHIFT, r = raw[2 * i + 1] >> MIC_SHIFT;
      dcL += (l - dcL) * 0.001f;
      dcR += (r - dcR) * 0.001f;
      sl += (l - dcL) * (l - dcL);
      sr += (r - dcR) * (r - dcR);
      float x = (l + r) * 0.5f, y = x - x1 + 0.995f * y1;   // L+R average as xelulu_fw does, DC blocked
      x1 = x;
      y1 = y;
      o[n] = micMute ? 0 : (int16_t)constrain(y, -32768.f, 32767.f);
    }
  }
  *got = n * 2;
  if (!n) return ESP_FAIL;
  micDbL += (10 * log10f(sl / n / 1073741824.0f + 1e-12f) - micDbL) * 0.3f;   // dBFS of int16
  micDbR += (10 * log10f(sr / n / 1073741824.0f + 1e-12f) - micDbR) * 0.3f;
  return ESP_OK;
}

static void srEvent(void*, sr_event_t ev, int cmd, int phrase) {   // ESP-SR handler task
  if (ev == SR_EVENT_COMMAND) {
    Heard h = {(int8_t)cmd, (int8_t)phrase};
    xQueueSend(heardQ, &h, 0);
  }
  if (ev == SR_EVENT_COMMAND || ev == SR_EVENT_TIMEOUT) sr_set_mode(SR_MODE_COMMAND);   // keep listening
}

static void voiceBegin() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.dma_desc_num = 8;
  cc.dma_frame_num = 256;   // 128 ms of slack for the feed task
  i2s_new_channel(&cc, nullptr, &rx);
  i2s_std_config_t sc = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {I2S_GPIO_UNUSED, (gpio_num_t)PIN_MIC_BCLK, (gpio_num_t)PIN_MIC_WS, I2S_GPIO_UNUSED,
                 (gpio_num_t)PIN_MIC_DATA, {false, false, false}},
  };
  i2s_channel_init_std_mode(rx, &sc);
  i2s_channel_enable(rx);
  heardQ = xQueueCreate(4, sizeof(Heard));
  // an empty or missing model partition would crash ESP-SR: check it first
  const esp_partition_t* mp = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "model");
  uint32_t head = 0xFFFFFFFF;
  if (mp) esp_partition_read(mp, 0, &head, 4);
  if (!mp || head == 0xFFFFFFFF) {
    Serial.println("[voice] OFF: no speech models. Build with PartitionScheme=esp_sr_16 (the upload writes srmodels.bin)");
    return;
  }
  // start in wake-word mode, then switch: that is the call that turns WakeNet off (esp32-hal-sr.c)
  srOk = sr_start(micFill, nullptr, SR_CHANNELS_MONO, SR_MODE_WAKEWORD, "M", PHRASES, N_PHRASES, srEvent, nullptr) == ESP_OK &&
         sr_set_mode(SR_MODE_COMMAND) == ESP_OK;
  Serial.printf("[voice] ESP-SR %s: %d phrases, no wake word\n", srOk ? "listening" : "FAILED", N_PHRASES);
}

// ============================================================
// wheels: slow steps, fenced. Pose from each step's measured mm (Drive.cpp's own odometry is private)
// ============================================================
static float px = 0, py = 0, pth = 0;   // mm, mm, rad from the fence centre; + th = robot's left
static struct { int8_t lin, turn; uint8_t ticket; } drv = {};
static bool driveOk = true;
static uint8_t waitTicket = 0;          // the body ticket a test step waits for

static uint8_t newTicket() {
  static uint8_t t = 0;
  if (++t == 0) t = 1;
  return t;
}

static void homeReset(const char* why) {
  px = py = pth = 0;
  lineSet(L_RESULT, TFT_CYAN, "new fence centre (%s)", why);
}

static uint32_t smileAt = 0;            // a voice reaction's face goes back to SMILE then

static void refuse(const char* why) {
  driveOk = false;
  lineSet(L_RESULT, TFT_RED, "refused: %s", why);
  faceSet(FACE_QUESTIONING);
  smileAt = millis() + 3000;
  bodyMotion(G_SHAKE, P_LOCAL);   // a head shake: "no"
}

// lin +1 forward / -1 back, or turn +1 left / -1 right; k = how many steps. false = refused
static bool wheelStep(int8_t lin, int8_t turn, uint8_t k) {
  uint8_t il = wheelInterlock(lin, false, millis());
  if (il) { refuse(interlockName(il)); return false; }
  if (lin) {
    float d = hypotf(px + lin * k * STEP_MM * cosf(pth), py + lin * k * STEP_MM * sinf(pth));
    if (d > FENCE_MM && d > hypotf(px, py)) {   // further out than FENCE_MM: refused (back toward the centre is fine)
      char b[32];
      snprintf(b, sizeof b, "fence (%d cm)", (int)(d / 10));
      refuse(b);
      return false;
    }
  }
  drv = {lin, turn, newTicket()};
  waitTicket = drv.ticket;
  driveOk = true;
  if (lin) bodyDrive(DM_LINEAR, lin * DRIVE_MMS, 0, k * STEP_MM, false, drv.ticket);
  else bodyDrive(DM_TURN, 0, (int16_t)(turn * TURN_RADS * 1000), (uint16_t)lroundf(k * TURN_DEG * DEG_TO_RAD * cal.wheelBaseMm / 2),
                 false, drv.ticket);
  lineSet(L_RESULT, TFT_GREEN, lin ? "%s %d mm" : "turn %s %d deg", lin ? (lin > 0 ? "forward" : "back") : (turn > 0 ? "left" : "right"),
          lin ? k * STEP_MM : k * TURN_DEG);
  return true;
}

static void stopAll() {
  bodyDrive(DM_STOP, 0, 0, 0, false, 0);
  bodyStop(P_SAFETY);
  sfxStop();
}

// ============================================================
// tests
// ============================================================
enum Test : uint8_t { T_SELF, T_SPEAKER, T_VOICE, T_HEAD, T_ARMS, T_GESTURES, T_FACES, T_DANCE, T_WHEELS, T_TOUCH, T_ALL, T_COUNT };
static const char* const TEST_NAMES[T_COUNT] = {"SELF CHECK", "SPEAKER", "VOICE WORDS", "HEAD SWEEP", "ARMS", "GESTURES",
                                                "EXPRESSIONS", "DANCES", "WHEEL SWEEP", "TOUCH + IMU", "RUN ALL"};
static const uint8_t ALL_LIST[] = {T_SELF, T_SPEAKER, T_HEAD, T_ARMS, T_GESTURES, T_FACES, T_DANCE, T_WHEELS};
static int8_t sel = T_SELF, running = -1, allIdx = -1;
static uint16_t stepN;
static uint32_t minAt, maxAt, menuQuietUntil;
static bool (*cond)() = nullptr;
static uint8_t heardMask;
static bool usbAtBoot;

static bool ticketDone() { return !waitTicket; }
static bool soundDone() { return !sfxBusy(); }
static bool wordsDone() { return heardMask == (1u << C_COUNT) - 1; }

// next step after minMs, and once c() is true (+ GAP_MS); maxMs gives up on c()
static void waitFor(uint32_t minMs, bool (*c)() = nullptr, uint32_t maxMs = 15000) {
  uint32_t now = millis();
  minAt = now + minMs;
  maxAt = now + maxMs;
  cond = c;
}

static void showMenu() {
  lineSet(L_TITLE, TFT_CYAN, "T%d/%d %s", sel + 1, T_COUNT, TEST_NAMES[sel]);
  lineSet(L_INFO, TFT_WHITE, "HEAD next  BELLY run  BACK prev");
}

static void showNextWord() {
  for (uint8_t c = 0; c < C_COUNT; c++)
    if (!(heardMask & (1 << c))) {
      lineSet(L_INFO, TFT_WHITE, "say \"%s\"  (%d/%d)", CMD_NAMES[c], __builtin_popcount(heardMask), C_COUNT);
      return;
    }
  lineSet(L_INFO, TFT_GREEN, "all %d words heard", C_COUNT);
}

static void selfCheck() {
  uint8_t ok = 0;
  for (uint8_t j = 0; j < JOINTS; j++) ok += !faultOn((Fault)(F_SERVO_MISSING + j)) && !faultOn((Fault)(F_SERVO_OVERVOLT + j));
  if (usbAtBoot) lineSet(L_INFO, TFT_ORANGE, "servos OFF: USB at boot");
  else lineSet(L_INFO, ok == JOINTS ? TFT_GREEN : TFT_RED, "servos %d/3  rail %.2f V", ok, S.railMv / 1000.0f);
  bool micOk = micDbL > -100 && micDbR > -100;
  bool good = !faultOn(F_TOUCH_MISSING) && !faultOn(F_IMU_MISSING) && micOk && srOk;
  lineSet(L_RESULT, good ? TFT_GREEN : TFT_RED, "touch %s imu %s mic %s voice %s", faultOn(F_TOUCH_MISSING) ? "NO" : "ok",
          faultOn(F_IMU_MISSING) ? "NO" : "ok", !srOk ? "?" : micOk ? "ok" : "NO", srOk ? "ok" : "NO");   // ESP-SR reads the mics
  Serial.printf("[self] servos %d/3 (usb at boot %d) rail %u mV | mic L %.0f R %.0f dBFS | faults 0x%08lx | heap %u psram %u\n", ok,
                usbAtBoot, S.railMv, micDbL, micDbR, (unsigned long)faultMask(), ESP.getFreeHeap(), ESP.getFreePsram());
}

// one step of test t; false = the test is over
static bool runStep(uint8_t t, uint16_t s) {
  switch (t) {
    case T_SELF:
      if (s) return false;
      selfCheck();
      sfxPlay(S_BOOT);
      waitFor(5000);
      return true;

    case T_SPEAKER: {
      struct Snd { bool clip; uint8_t id; };
      static const Snd SEQ[] = {{0, S_BOOT}, {1, CLIP_HELLO}, {1, CLIP_HEHE}, {1, CLIP_WHAT}, {0, S_POP}, {0, S_PURR},
                                {0, S_CLAP}, {0, S_LISTEN}, {0, FACE_LAUGH}, {0, FACE_SCARED}, {0, FACE_LOVING}};
      const uint8_t N = sizeof SEQ / sizeof SEQ[0];
      if (s >= N) return false;
      if (SEQ[s].clip) sfxPlayClip((ClipId)SEQ[s].id);
      else sfxPlay(SEQ[s].id);
      lineSet(L_INFO, TFT_WHITE, "%d/%d %s", s + 1, N, SEQ[s].clip ? CLIP_NAMES[SEQ[s].id] : SOUND_NAMES[SEQ[s].id]);
      waitFor(150, soundDone, 8000);
      return true;
    }

    case T_VOICE:   // dry run: words are ticked off, nothing moves
      if (s == 0) { heardMask = 0; showNextWord(); waitFor(500, wordsDone, 60000); return true; }
      if (s == 1) { sfxPlay(wordsDone() ? FACE_ECSTATIC : FACE_DISAPPOINTED); showNextWord(); waitFor(3000); return true; }
      return false;

    case T_HEAD: {
      static const char* const N[] = {"centre", "left limit", "right limit", "centre"};
      if (s >= 4) return false;
      int16_t a = s == 1 ? cal.softMax[HP] : s == 2 ? cal.softMin[HP] : 0;
      bool end = s == 1 || s == 2;
      bodyPose(a, 10, 10, end ? 2500 : 1200, P_LOCAL);
      lineSet(L_INFO, TFT_WHITE, "%s %+d deg", N[s], a);
      waitFor(end ? 3500 : 2000);
      return true;
    }

    case T_ARMS: {
      struct Pose { int16_t l, r; const char* n; };
      static const Pose SEQ[] = {{150, 10, "left arm up"}, {10, 10, "rest"}, {10, 150, "right arm up"},
                                 {10, 10, "rest"}, {90, 90, "both forward"}, {10, 10, "rest"}};
      if (s >= 6) return false;
      bodyPose(0, SEQ[s].l, SEQ[s].r, 1200, P_LOCAL);
      lineSet(L_INFO, TFT_WHITE, "%s  L %d R %d", SEQ[s].n, SEQ[s].l, SEQ[s].r);
      waitFor(2000);
      return true;
    }

    case T_GESTURES:
      if (s >= G_COUNT) return false;
      lineSet(L_INFO, TFT_WHITE, "%d/%d %s", s + 1, G_COUNT, GESTURE_NAMES[s]);
      waitTicket = newTicket();
      bodyMotion(s, P_LOCAL, 0, waitTicket);
      waitFor(300, ticketDone, 8000);
      return true;

    case T_FACES:   // each expression with its own body movement and sound (FW-SOT §7.4)
      if (s >= FACE_COUNT) { faceSet(FACE_SMILE); return false; }
      bodyWake();   // SLEEPING / UPDATING leave the servos limp
      faceSet((FaceId)s);
      sfxPlayFace((FaceId)s);
      lineSet(L_INFO, TFT_WHITE, "%d/%d %s", s + 1, FACE_COUNT, FACE_NAMES[s]);
      waitTicket = newTicket();
      bodyMotion(MOT_FACE(s), P_LOCAL, 0, waitTicket);
      waitFor(2500, ticketDone, 5000);   // looping motions are cut at 5 s
      return true;

    case T_DANCE:
      if (s >= 10) { faceSet(FACE_SMILE); return false; }
      faceSet(FACE_ECSTATIC);
      sfxPlayFace(FACE_ECSTATIC);
      lineSet(L_INFO, TFT_WHITE, "pattern %d/10, 2 bars at 100 bpm", s + 1);
      waitTicket = newTicket();
      bodyDance(s, 100, 2, P_LOCAL, waitTicket);
      waitFor(500, ticketDone, 10000);
      return true;

    case T_WHEELS: {   // a small sweep that ends where it started
      struct Mv { int8_t lin, turn; uint8_t k; };
      static const Mv SEQ[] = {{1, 0, 1}, {-1, 0, 1}, {0, 1, 1}, {0, -1, 2}, {0, 1, 1}};
      const uint8_t N = sizeof SEQ / sizeof SEQ[0];
      if (s && !driveOk) return false;   // a step was refused or stopped early: the reason is on screen
      if (s == N) {
        lineSet(L_INFO, TFT_WHITE, "done: %d mm from start, %+d deg", (int)hypotf(px, py), (int)lroundf(pth * RAD_TO_DEG));
        waitFor(3000);
        return true;
      }
      if (s > N) return false;
      lineSet(L_INFO, TFT_WHITE, "step %d/%d", s + 1, N);
      if (wheelStep(SEQ[s].lin, SEQ[s].turn, SEQ[s].k)) waitFor(200, ticketDone, 8000);
      else waitFor(2500);
      return true;
    }

    case T_TOUCH:
      if (s) return false;
      lineSet(L_INFO, TFT_WHITE, "pads, lift, tilt, shake: 60 s");
      lineSet(L_RESULT, TFT_WHITE, "say \"stop\" to end early");
      waitFor(60000);
      return true;
  }
  return false;
}

static void startTest(uint8_t t) {
  if (t == T_ALL) { allIdx = 0; t = ALL_LIST[0]; }
  running = t;
  stepN = 0;
  cond = nullptr;
  waitTicket = 0;
  minAt = millis();
  bodyWake();
  lineSet(L_TITLE, TFT_CYAN, allIdx >= 0 ? "T%d %s (all %d/%d)" : "T%d %s", t + 1, TEST_NAMES[t], allIdx + 1, (int)sizeof ALL_LIST);
  lineTxt[L_INFO][0] = 0;
  Serial.printf("[test] T%d %s start\n", t + 1, TEST_NAMES[t]);
}

static void endTest(bool aborted) {
  int8_t t = running;
  running = -1;
  cond = nullptr;
  waitTicket = 0;
  bodyWake();
  faceSet(FACE_SMILE);
  Serial.printf("[test] T%d %s %s\n", t + 1, TEST_NAMES[t], aborted ? "ABORTED" : "done");
  if (allIdx >= 0 && !aborted && ++allIdx < (int8_t)sizeof ALL_LIST) { startTest(ALL_LIST[allIdx]); return; }
  allIdx = -1;
  lineSet(L_RESULT, aborted ? TFT_ORANGE : TFT_GREEN, "T%d %s", t + 1, aborted ? "aborted" : "done");
  menuQuietUntil = millis() + 800;   // the touch that aborted must not also step the menu
  showMenu();
}

static void runner(uint32_t now) {
  if (running < 0) return;
  if (running == T_HEAD) {   // live head feedback
    static uint32_t at;
    if (now - at > 200) { at = now; lineSet(L_RESULT, TFT_WHITE, "head now %+d deg", S.headDeg); }
  }
  if (cond && cond()) {
    cond = nullptr;
    if ((int32_t)(minAt - (now + GAP_MS)) < 0) minAt = now + GAP_MS;
  }
  bool timedOut = cond && (int32_t)(now - maxAt) >= 0;
  if ((cond || (int32_t)(now - minAt) < 0) && !timedOut) return;
  if (timedOut && waitTicket) {   // a looping motion, or a step that never ended
    bodyDrive(DM_STOP, 0, 0, 0, false, 0);
    bodyStop(P_SAFETY);
    waitTicket = 0;
  }
  cond = nullptr;
  if (!runStep(running, stepN++)) endTest(false);
}

// ============================================================
// inputs
// ============================================================
static void heard(int8_t cmd, int phrase) {
  if (cmd < 0 || cmd >= C_COUNT) return;
  const char* said = phrase >= 0 && phrase < N_PHRASES ? PHRASES[phrase].str : CMD_NAMES[cmd];
  lineSet(L_HEARD, TFT_YELLOW, "heard: \"%s\"", said);
  heardMask |= 1 << cmd;
  if (running == T_VOICE) { showNextWord(); return; }   // dry run
  if (cmd == C_STOP) {
    stopAll();
    if (running >= 0) endTest(true);
    lineSet(L_RESULT, TFT_GREEN, "stopped");
    return;
  }
  if (running >= 0) { lineSet(L_RESULT, TFT_ORANGE, "busy: T%d runs (say stop)", running + 1); return; }
  switch (cmd) {
    case C_FORWARD:  sfxPlay(S_POP); wheelStep(1, 0, 1); break;
    case C_BACKWARD: sfxPlay(S_POP); wheelStep(-1, 0, 1); break;
    case C_LEFT:     sfxPlay(S_POP); wheelStep(0, 1, 1); break;
    case C_RIGHT:    sfxPlay(S_POP); wheelStep(0, -1, 1); break;
    case C_HELLO:
      faceSet(FACE_WINK);
      smileAt = millis() + 3000;
      sfxPlayClip(CLIP_HELLO);
      bodyMotion(G_WAVE, P_LOCAL);
      lineSet(L_RESULT, TFT_GREEN, "hello! (wave)");
      break;
    case C_DANCE: {
      uint8_t p = esp_random() % 10;
      faceSet(FACE_ECSTATIC);
      smileAt = millis() + 6000;
      sfxPlayFace(FACE_ECSTATIC);
      bodyDance(p, 110, 2, P_LOCAL);
      lineSet(L_RESULT, TFT_GREEN, "dance pattern %d", p + 1);
      break;
    }
  }
}

static const char* const GEST_NAMES[] = {"tap", "double tap", "hold", "pat"};
static const char* const WM_NAMES[] = {"picked up", "shake", "upside down", "drop", "tilt"};

static void onSense(const SenseEvent& e, uint32_t now) {
  bool padTest = running == T_TOUCH;
  switch (e.kind) {
    case SE_TOUCH_DOWN:
      faceSquash();
      if (e.a == Z_BACK && !padTest) {   // BACK = emergency stop, in the menu too
        stopAll();
        if (running >= 0) endTest(true);
      }
      break;
    case SE_TOUCH:
      if (padTest) { lineSet(L_RESULT, TFT_GREEN, "%s: %s", zoneName(e.a), GEST_NAMES[e.b & 3]); sfxPlay(S_POP); break; }
      if (running >= 0 || e.b != TG_TAP || now < menuQuietUntil) break;
      if (e.a == Z_BELLY) { startTest(sel); break; }
      if (e.a == Z_HEAD) sel = (sel + 1) % T_COUNT;
      if (e.a == Z_BACK) sel = (sel + T_COUNT - 1) % T_COUNT;
      showMenu();
      break;
    case SE_MOTION:
      if (e.a == WM_PICKUP || padTest)
        lineSet(L_RESULT, TFT_ORANGE, e.a == WM_PICKUP ? "lifted: wheels locked" : "imu: %s", WM_NAMES[e.a % 5]);
      break;
    case SE_PUT_DOWN: homeReset("set down"); break;
    case SE_FALLING: lineSet(L_RESULT, TFT_RED, "imu: falling"); break;
    default:
      if (padTest) {
        static const char* const K[] = {"", "", "", "", "", "falling", "impact", "put down", "upright", "level again", "spin", "knock", "carried"};
        if (e.kind < sizeof K / sizeof K[0] && K[e.kind][0]) lineSet(L_RESULT, TFT_ORANGE, "imu: %s", K[e.kind]);
      }
  }
}

static void onBody(const BodyEvent& e, uint32_t now) {
  if (e.kind == BE_DRIVE_DONE && e.ticket && e.ticket == drv.ticket) {
    if (drv.lin) { px += drv.lin * e.b * cosf(pth); py += drv.lin * e.b * sinf(pth); }
    else pth += drv.turn * e.b / (cal.wheelBaseMm / 2);
    driveOk = e.ok;
    if (e.ok) lineSet(L_RESULT, TFT_GREEN, drv.lin ? "moved %d mm, %d cm from centre" : "turned %d deg",
                      drv.lin ? e.b : (int)lroundf(e.b / (cal.wheelBaseMm / 2) * RAD_TO_DEG), (int)(hypotf(px, py) / 10));
    else {
      uint8_t il = wheelInterlock(drv.lin, false, now);
      lineSet(L_RESULT, TFT_RED, "stopped after %d mm: %s", e.b, il ? interlockName(il) : "stop or stall");
    }
    drv.ticket = 0;
  }
  if ((e.kind == BE_MOTION_DONE || e.kind == BE_DRIVE_DONE) && e.ticket && e.ticket == waitTicket) waitTicket = 0;
  if (e.kind == BE_FAULT && e.a < F_COUNT) lineSet(L_RESULT, TFT_RED, "fault: %s", FAULT_NAMES[e.a]);
}

static void console() {   // USB boots only
  static char buf[40];
  static uint8_t n = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') { if (n < sizeof buf - 1) buf[n++] = c; continue; }
    buf[n] = 0;
    n = 0;
    if (!strncmp(buf, "t ", 2)) {
      int t = atoi(buf + 2) - 1;
      if (t >= 0 && t < T_COUNT) {
        if (running >= 0) { stopAll(); endTest(true); }
        startTest(t);
      }
    } else if (!strncmp(buf, "say ", 4)) {
      for (uint8_t k = 0; k < C_COUNT; k++) if (!strcmp(buf + 4, CMD_NAMES[k])) heard(k, -1);
    } else if (!strcmp(buf, "stop")) heard(C_STOP, -1);
    else if (!strcmp(buf, "home")) homeReset("console");
    else {
      Serial.println("t N = run test N:");
      for (uint8_t k = 0; k < T_COUNT; k++) Serial.printf("  t %d  %s\n", k + 1, TEST_NAMES[k]);
      Serial.println("say forward|backward|left|right|stop|hello|dance = as if heard · stop · home = new fence centre");
    }
  }
}

// ============================================================
static volatile bool bootDone = false;

static void appTask(void*) {   // core 0: face + tests + inputs, so ESP-SR's detect task keeps core 1
  faceBegin();
  faceSet(FACE_SMILE);
  while (!bootDone) { faceUpdate(millis()); vTaskDelay(1); }
  if (!srOk) lineSet(L_HEARD, TFT_RED, "voice OFF (see console)");
  startTest(T_SELF);
  uint32_t beat = 0;
  for (;;) {
    uint32_t now = millis();
    Heard h;
    while (xQueueReceive(heardQ, &h, 0) == pdTRUE) heard(h.cmd, h.phrase);
    SenseEvent se;
    while (senseNext(se)) onSense(se, now);
    BodyEvent be;
    while (bodyNext(be)) onBody(be, now);
    runner(now);
    console();
    if (smileAt && (int32_t)(now - smileAt) >= 0) {
      smileAt = 0;
      if (running < 0) faceSet(FACE_SMILE);
    }
    faceUpdate(now);
    if (now - beat >= 5000) {
      beat = now;
      Serial.printf("[test12] fps %u  mic L %.0f R %.0f dBFS%s  pose %.0f,%.0f mm %+.0f deg  lifted %d tilt %d\n", faceFps(), micDbL,
                    micDbR, micMute ? " (muted)" : "", px, py, pth * RAD_TO_DEG, S.lifted, S.tiltDeg);
    }
    vTaskDelay(1);
  }
}

void setup() {
  for (uint8_t p : {PIN_MOT_L_PWM, PIN_MOT_L_IN1, PIN_MOT_L_IN2, PIN_MOT_R_PWM, PIN_MOT_R_IN1, PIN_MOT_R_IN2}) {
    pinMode(p, OUTPUT);   // FIRST: STBY is tied high, so these LOW keep the wheels still
    digitalWrite(p, LOW);
  }
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);   // never block on a USB console nobody reads
  for (uint32_t t = millis(); !Serial && millis() - t < 1500;) delay(10);
  usbAtBoot = Serial.isPlugged();
  while (millis() < 600) delay(10);   // the panel wants ~0.5 s of power before its backlight (Display_test_code)
  Serial.printf("\n[test12] offline test, xelulu_fw %s modules. Hands OFF the pads while it boots\n", FW_VER);
  applyTestCal();
  Serial.printf("[test12] head centre %d counts, limits %+d..%+d deg (test1: %.1f..%.1f) · wheels %d mm/s, fence %d mm\n",
                cal.center[HP], cal.softMin[HP], cal.softMax[HP], HEAD_MID_DEG + HEAD_DIR * cal.softMin[HP],
                HEAD_MID_DEG + HEAD_DIR * cal.softMax[HP], DRIVE_MMS, FENCE_MM);
  lineSet(L_TITLE, TFT_CYAN, "XELULU OFFLINE TEST");
  lineSet(L_INFO, TFT_WHITE, "starting...");
  speakerBegin();
  senseBegin();   // touch baselines are learnt now: hands off
  bodyBegin();    // servo scan unless USB is plugged in
  xTaskCreatePinnedToCore(appTask, "app", 12288, nullptr, 1, nullptr, 0);
  voiceBegin();   // ~1-2 s: the models load from flash
  delay(1500);    // let the mic levels settle before the self check reads them
  bootDone = true;
}

void loop() { vTaskDelete(nullptr); }   // everything runs in its own task
