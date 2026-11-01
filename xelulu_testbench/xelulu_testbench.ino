// ============================================================
// Xelulu test bench, wired per Xelulu_Wiring.md (2-ToF version). It runs by itself
// after flashing and tells you what to do on the robot's own screen:
//   1. servo IDs, only if IDs 1, 2, 3 don't already answer: unplug the servos, then plug them in
//      one at a time when asked: LEFT ARM -> ID 2, RIGHT ARM -> ID 3, HEAD -> ID 1. Then unplug
//      and replug all 3 once: proves the IDs were saved in the servos (they survive power-off)
//   2. the servos move one after the other: left arm, right arm, head
//   3. after a 5 s countdown: the left N20 forward then back, then the right one,
//      each run reporting the encoder counts it saw
//   4. audio: both mics alive? speaker chime, then a 1 kHz tone the mics must hear (amp + speaker
//      + mics in one go), then tap the LEFT mic, then the RIGHT one: the tapped side must be louder
//   5. all the time, live: ToF x2, touch pads, gyro + accel, wheel encoders, servos, mic L/R bars
// At boot it tests the servo line itself (GPIO20 -> 1 kOhm -> SIGNAL <- GPIO19). If the line
// can't work, it says why, skips the servo steps and goes straight to the wheels.
// Console (same messages + a live line), 115200, on the board's CH340 "COM" port. The native
// USB port is out of use: its pins (19/20) carry the servo bus, so never plug a cable into it.
//   Keys: a = everything again, i = set servo IDs again, s = servos again,
//   w = wheels again, m = audio again, x = skip a step that is waiting on you.
//
// Board settings = xelulu_fw's:
//   FQBN esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,CDCOnBoot=cdc,USBMode=hwcdc,FlashMode=qio
// Libraries: Adafruit GFX + Adafruit ST7735 and ST7789 + VL53L0X (Pololu). The display is set
// up as in Display_test_code (the setup that works), not with TFT_eSPI: there is no MISO wire,
// and TFT_eSPI's no-MISO setup stops the S3's SPI bus.
// ============================================================
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <VL53L0X.h>
#include <ESP_I2S.h>
#include "driver/pulse_cnt.h"

// ---- pins: Xelulu_Wiring.md §11 ----
#define PIN_I2C_SDA     8    // MPR121, GY-87, TCA9548A
#define PIN_I2C_SCL     9
#define PIN_TFT_SCLK   12    // ST7789; no MISO (GPIO13 is the TB6612's BIN1)
#define PIN_TFT_MOSI   11
#define PIN_TFT_CS     10
#define PIN_TFT_DC     14
#define PIN_TFT_RST    21
#define PIN_TFT_EN     45    // backlight, HIGH = on
#define PIN_SERVO_TX   20    // -> 1 kOhm -> SIGNAL point
#define PIN_SERVO_RX   19    // SIGNAL point, plain wire. 19/20 = native USB D-/D+: as GPIOs, IDF turns USB off
static const uint8_t MOT_PWM[2] = {1, 41}, MOT_IN1[2] = {2, 13}, MOT_IN2[2] = {42, 47};   // TB6612 A = left, B = right
static const uint8_t ENC_C1[2] = {17, 18}, ENC_C2[2] = {48, 3};                          // left, right
#define PIN_MIC_BCLK    4    // both SPH0645: SEL GND = left slot, SEL 3.3 V = right slot
#define PIN_MIC_WS      5
#define PIN_MIC_DIN     6
#define PIN_SPK_BCLK   15    // MAX98357, SD on 3.3 V
#define PIN_SPK_WS     16
#define PIN_SPK_DOUT    7

// ---- knobs ----
#define SPK_VOLUME      0.25f   // 0..1 of full scale; GAIN pin open = 9 dB on the amp
#define TONE_HZ         1000    // loopback tone: an exact Goertzel bin at 32 kHz / 256
#define HEARD_DB        10      // the mics must hear the tone this far above silence
#define TAP_DB          15      // a tap must be this loud over the room...
#define SIDE_DB         4       // ...and this much louder on the tapped side
#define SERVO_SWING_DEG 20      // each way from where a servo rests; lower it if an arm hits the body
#define WHEEL_DUTY      0.6f    // N20 drive, 0..1
#define WAIT_MS         60000   // a step waiting on you gives up after this (or on `x`)

struct Joint { const char* name; uint8_t id; };
static const Joint ORDER[3] = {{"LEFT ARM", 2}, {"RIGHT ARM", 3}, {"HEAD", 1}};   // §10 IDs; also the move order
struct Fb { int16_t pos; uint8_t volt10, temp; };
struct MicSum { double pow[2], tone[2]; uint32_t n, flat[2]; };   // what the mics heard since the last micTake()

#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_YELLOW 0xFFE0
#define C_CYAN   0x07FF
#define C_GREEN  0x07E0
#define C_RED    0xF800
#define C_GREY   0x4208

// ---- live readings: liveTick() keeps them fresh and on screen ----
static const char* lineFault = nullptr;    // why the servo line can't work, nullptr = it works
static VL53L0X tof[2];                     // mux CH0 front-left, CH1 front-right
static bool tofUp[2], touchUp, imuUp, echo, servoOk[4];
static uint16_t tofMm[2], padBits;
static float gyro[3], acc[3];
static pcnt_unit_handle_t enc[2];
static int encCount[2];
static Fb servo[4];                        // by bus ID 1-3
static const char* const PAD[5] = {"HEAD", "BACK", "BELLY", "L-HND", "R-HND"};   // MPR121 ELE0-4

static int key() {   // one console key, -1 if none
  int c = Serial0.read();
  return c < 0 ? c : tolower(c);
}

// ============================================================ display: ST7789 320x240 landscape
// Set up as Display_test_code. Text is drawn into a 320x20 strip in RAM and pushed in one go: no flicker.
static Adafruit_ST7789 tft(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
static GFXcanvas16 strip(320, 20);
static char shown[2][28];   // the status rows on screen now

static void pushStrip(int y) { tft.drawRGBBitmap(0, y, strip.getBuffer(), 320, 20); }
static void row(int y, uint16_t color, const char* fmt, ...) {   // one row of big text, 26 characters
  char b[40];
  va_list a;
  va_start(a, fmt);
  vsnprintf(b, sizeof b, fmt, a);
  va_end(a);
  strip.fillScreen(C_BLACK);
  strip.setTextSize(2);
  strip.setTextColor(color);
  strip.setCursor(4, 2);
  strip.print(b);
  pushStrip(y);
}
static void tftBegin() {   // Display_test_code's sequence, step for step
  delay(500);                       // let the 5 V and 3.3 V rails settle
  pinMode(PIN_TFT_EN, OUTPUT);      // backlight on: a sharp 3.3 V step on EN
  digitalWrite(PIN_TFT_EN, HIGH);
  SPI.begin(PIN_TFT_SCLK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
  pinMode(PIN_TFT_RST, OUTPUT);
  digitalWrite(PIN_TFT_RST, HIGH);
  delay(10);
  digitalWrite(PIN_TFT_RST, LOW);
  delay(15);
  digitalWrite(PIN_TFT_RST, HIGH);
  delay(150);
  tft.init(240, 320);
  tft.setRotation(1);               // landscape, 320x240
  tft.fillScreen(C_BLACK);
}
// What to do / what is happening: two yellow rows; the console gets it whenever it changes.
static void status(const char* l1, const char* l2) {
  if (strcmp(shown[0], l1) || strcmp(shown[1], l2)) Serial0.printf(">> %s %s\n", l1, l2);
  strlcpy(shown[0], l1, sizeof shown[0]);
  strlcpy(shown[1], l2, sizeof shown[1]);
  row(22, C_YELLOW, "%s", shown[0]);
  row(42, C_YELLOW, "%s", shown[1]);
}

// ============================================================ sensors: I2C on GPIO 8/9, 400 kHz as xelulu_fw
#define MPR 0x5A
#define MPU 0x68

static bool i2cWrite(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}
static bool i2cRead(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(addr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}
static void muxSelect(uint8_t ch) {   // TCA9548A
  Wire.beginTransmission(0x70);
  Wire.write(1 << ch);
  Wire.endTransmission();
}

static bool touchBegin() {   // xelulu_fw sense/Touch.cpp's setup: ELE0-4, Config.h thresholds (2026-10-31 cal)
  static const uint8_t THR[5] = {21, 24, 12, 6, 6}, REL[5] = {10, 12, 6, 3, 3};   // head, back, belly, L/R hand
  i2cWrite(MPR, 0x80, 0x63);   // soft reset
  delay(1);
  i2cWrite(MPR, 0x5E, 0x00);   // stop mode: config is writable only here
  uint8_t c2 = 0;
  if (!i2cRead(MPR, 0x5D, &c2, 1) || c2 != 0x24) return false;
  for (uint8_t e = 0; e < 5; e++) {
    i2cWrite(MPR, 0x41 + 2 * e, THR[e]);
    i2cWrite(MPR, 0x42 + 2 * e, REL[e]);
  }
  static const uint8_t REGS[][2] = {
    {0x2B, 0x01}, {0x2C, 0x01}, {0x2D, 0x0E}, {0x2E, 0x00}, {0x2F, 0x01}, {0x30, 0x01},   // slow falling
    {0x31, 0xFF}, {0x32, 0x02}, {0x33, 0x00}, {0x34, 0x00}, {0x35, 0x00}, {0x5B, 0x00},   // filter, = Touch.cpp
    {0x5C, 0x10}, {0x5D, 0x20}, {0x7B, 0x0B}, {0x7D, 200}, {0x7E, 130}, {0x7F, 180},
  };
  for (auto& r : REGS) i2cWrite(MPR, r[0], r[1]);
  return i2cWrite(MPR, 0x5E, 0x85);   // run ELE0-4, baseline from the first reading
}
static int padSignal(uint8_t e) {    // baseline - filtered: how strongly ELEe is touched
  uint8_t f[2], b;
  if (!i2cRead(MPR, 0x04 + 2 * e, f, 2) || !i2cRead(MPR, 0x1E + e, &b, 1)) return 0;
  return (b << 2) - (f[0] | (f[1] & 3) << 8);
}

static void sensorsBegin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  Wire.setTimeOut(25);
  touchUp = touchBegin();
  uint8_t who = 0;
  imuUp = i2cRead(MPU, 0x75, &who, 1);
  if (imuUp) {
    i2cWrite(MPU, 0x6B, 0x01);   // wake; then xelulu_fw's ranges: DLPF 44 Hz, +-1000 deg/s, +-4 g
    i2cWrite(MPU, 0x1A, 0x03);
    i2cWrite(MPU, 0x1B, 0x10);
    i2cWrite(MPU, 0x1C, 0x08);
  }
  for (uint8_t ch = 0; ch < 2; ch++) {
    muxSelect(ch);
    uint8_t id = 0;
    tof[ch].setBus(&Wire);
    tof[ch].setTimeout(100);
    tofUp[ch] = i2cRead(0x29, 0xC0, &id, 1) && id == 0xEE && tof[ch].init();
    if (tofUp[ch]) tof[ch].startContinuous();
  }
  Serial0.printf("[sensors] MPR121 %s  MPU6050 %s  ToF front-left %s  ToF front-right %s\n", touchUp ? "ok" : "MISSING",
              imuUp ? "ok" : "MISSING", tofUp[0] ? "ok" : "MISSING", tofUp[1] ? "ok" : "MISSING");
}

static void readSensors() {
  uint8_t b[14];
  if (touchUp && i2cRead(MPR, 0x00, b, 2)) padBits = (b[0] | b[1] << 8) & 0x1F;
  if (imuUp && i2cRead(MPU, 0x3B, b, 14))
    for (int i = 0; i < 3; i++) {
      acc[i] = (int16_t)(b[2 * i] << 8 | b[2 * i + 1]) / 8192.0f;      // +-4 g
      gyro[i] = (int16_t)(b[8 + 2 * i] << 8 | b[9 + 2 * i]) / 32.8f;   // +-1000 deg/s
    }
  for (uint8_t ch = 0; ch < 2; ch++) {   // as xelulu_fw sense/Tof.cpp: a reading that isn't ready waits for next time
    if (!tofUp[ch]) continue;
    muxSelect(ch);
    if ((tof[ch].readReg(VL53L0X::RESULT_INTERRUPT_STATUS) & 0x07) == 0) continue;
    tofMm[ch] = tof[ch].readReg16Bit(VL53L0X::RESULT_RANGE_STATUS + 10);
    tof[ch].writeReg(VL53L0X::SYSTEM_INTERRUPT_CLEAR, 0x01);
  }
  for (uint8_t m = 0; m < 2; m++) pcnt_unit_get_count(enc[m], &encCount[m]);
}

// ============================================================ audio: 2x SPH0645 on I2S0 (stereo), MAX98357 on I2S1
// Two tasks on core 0: one keeps reading the mics, one keeps the amp fed (silence unless toneHz is set).
#define AUDIO_HZ 32000   // SPH0645 wants a 1.024-4.096 MHz BCLK: 32 kHz x 64 bits = 2.048 MHz
#define BLOCK    256
static I2SClass mic(I2S_NUM_0), spk(I2S_NUM_1);
static bool micUp, spkUp;
static volatile float toneHz;              // 0 = silence
static float micDb[2] = {-120, -120};      // level of the last block, dBFS: the live bars
static bool micFlat[2];                    // last block was one value over and over: nobody drives that slot
static MicSum micSum;
static portMUX_TYPE micMux = portMUX_INITIALIZER_UNLOCKED;
static const float FS2 = 131072.0f * 131072.0f / 2;   // mean square of a full-scale 18-bit sine

static float db(double meanSq) { return 10 * log10(meanSq / FS2 + 1e-12); }
static MicSum micTake() {   // what the mics heard since the last call
  taskENTER_CRITICAL(&micMux);
  MicSum s = micSum;
  micSum = {};
  taskEXIT_CRITICAL(&micMux);
  if (!s.n) s.n = 1;        // nothing yet: all zeros, reads as silence
  return s;
}
static void micTask(void*) {
  static int32_t raw[BLOCK * 2];   // L, R, L, R...: SEL GND = left slot
  const float k = 2 * cosf(2 * PI * TONE_HZ / AUDIO_HZ);
  for (;;) {
    if (mic.readBytes((char*)raw, sizeof raw) != sizeof raw) { delay(10); continue; }
    float pow[2], tone[2];
    bool flat[2];
    for (int c = 0; c < 2; c++) {
      float mean = 0;
      flat[c] = true;
      for (int i = 0; i < BLOCK; i++) {
        mean += raw[2 * i + c] >> 14;   // 18-bit sample, top-aligned in the 32-bit slot
        flat[c] &= raw[2 * i + c] == raw[c];
      }
      mean /= BLOCK;
      float p = 0, s1 = 0, s2 = 0;
      for (int i = 0; i < BLOCK; i++) {
        float x = (raw[2 * i + c] >> 14) - mean, s0 = x + k * s1 - s2;   // DC off; Goertzel at TONE_HZ
        p += x * x;
        s2 = s1;
        s1 = s0;
      }
      pow[c] = p / BLOCK;
      tone[c] = (s1 * s1 + s2 * s2 - k * s1 * s2) / (BLOCK * BLOCK / 2.0f);   // same scale as pow
      micDb[c] = db(pow[c]);
      micFlat[c] = flat[c];
    }
    taskENTER_CRITICAL(&micMux);
    for (int c = 0; c < 2; c++) {
      micSum.pow[c] += pow[c];
      micSum.tone[c] += tone[c];
      micSum.flat[c] += flat[c];
    }
    micSum.n++;
    taskEXIT_CRITICAL(&micMux);
  }
}
static void spkTask(void*) {
  static int16_t buf[BLOCK * 2];   // same sample on both slots: SD on 3.3 V plays the left one
  float ph = 0;
  for (;;) {
    float f = toneHz, a = f > 0 ? SPK_VOLUME * 32767 : 0;
    for (int i = 0; i < BLOCK; i++) {
      buf[2 * i] = buf[2 * i + 1] = a * sinf(ph);
      ph += 2 * PI * f / AUDIO_HZ;
      if (ph > 2 * PI) ph -= 2 * PI;
    }
    spk.write((uint8_t*)buf, sizeof buf);   // blocks until the DMA has room: paces the task
  }
}
static void audioBegin() {
  mic.setPins(PIN_MIC_BCLK, PIN_MIC_WS, -1, PIN_MIC_DIN);
  micUp = mic.begin(I2S_MODE_STD, AUDIO_HZ, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
  spk.setPins(PIN_SPK_BCLK, PIN_SPK_WS, PIN_SPK_DOUT);
  spkUp = spk.begin(I2S_MODE_STD, AUDIO_HZ, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  if (micUp) xTaskCreatePinnedToCore(micTask, "mic", 4096, nullptr, 2, nullptr, 0);
  if (spkUp) xTaskCreatePinnedToCore(spkTask, "spk", 4096, nullptr, 2, nullptr, 0);
  Serial0.printf("[audio] mic I2S %s  speaker I2S %s\n", micUp ? "ok" : "FAILED", spkUp ? "ok" : "FAILED");
}

// ============================================================ servo bus: 3x SC09, SCS protocol, 1 Mbps half-duplex
// Same protocol as xelulu_fw body/Scs.cpp: the ESP hears its own request through the
// 1 kOhm join first, so that echo is dropped before the reply is read.
static HardwareSerial& bus = Serial1;

static uint8_t scsSum(const uint8_t* p, uint8_t n) {
  uint8_t s = 0;
  while (n--) s += *p++;
  return ~s;
}
// One request. -1 = nobody answered, 0 = garbled (two servos with one ID talk at once), 1 = valid reply
static int scs(uint8_t id, uint8_t inst, const uint8_t* par, uint8_t np, uint8_t* resp, uint8_t respLen) {
  uint8_t pkt[24] = {0xFF, 0xFF, id, (uint8_t)(np + 2), inst};
  uint8_t n = np + 6;
  if (np) memcpy(pkt + 5, par, np);
  pkt[n - 1] = scsSum(pkt + 2, np + 3);
  while (bus.available()) bus.read();
  bus.write(pkt, n);
  bus.flush();                                // until the last stop bit is out
  uint8_t buf[48], got = 0, want = (echo ? n : 0) + respLen;
  for (uint32_t t0 = micros(); got < want && micros() - t0 < 3000;) {
    int c = bus.read();
    if (c >= 0) buf[got++] = c;
  }
  uint8_t off = echo && got >= n && !memcmp(buf, pkt, n) ? n : 0;
  if (got <= off) return -1;
  const uint8_t* r = buf + off;
  if (got - off < respLen || r[0] != 0xFF || r[1] != 0xFF || r[2] != id || r[3] != respLen - 4 ||
      r[respLen - 1] != scsSum(r + 2, respLen - 3)) return 0;
  memcpy(resp, r, respLen);
  return 1;
}
static int scsRead(uint8_t id, uint8_t addr, uint8_t* out, uint8_t n) {
  const uint8_t p[2] = {addr, n};
  uint8_t r[16];
  int k = scs(id, 0x02, p, 2, r, n + 6);
  if (k == 1) memcpy(out, r + 5, n);
  return k;
}
static int scsWrite(uint8_t id, uint8_t addr, const uint8_t* d, uint8_t n) {
  uint8_t p[8] = {addr}, r[6];
  memcpy(p + 1, d, n);
  return scs(id, 0x03, p, n + 1, r, 6);
}
static bool scsFb(uint8_t id, Fb& f) {   // present position, supply voltage, temperature
  uint8_t b[8];
  if (scsRead(id, 56, b, 8) != 1) return false;
  f.pos = b[0] << 8 | b[1];   // big-endian on the SCSCL family
  f.volt10 = b[6];
  f.temp = b[7];
  return true;
}
static void scsGoal(uint8_t id, int pos) {   // goal time 0, speed 0: follow the setpoints we stream
  pos = constrain(pos, 20, 1003);
  const uint8_t g[6] = {(uint8_t)(pos >> 8), (uint8_t)pos, 0, 0, 0, 0};
  scsWrite(id, 42, g, 6);
}
static void scsTorque(uint8_t id, bool on) {
  uint8_t v = on;
  scsWrite(id, 40, &v, 1);
}
// Unlock EEPROM, write, lock under the new ID. The EEPROM write takes several ms: locking too
// soon left only the RAM copy changed, so the ID went back to the old one at the next power-up.
static bool scsSetId(uint8_t id, uint8_t newId) {
  uint8_t unlock = 0, lock = 1, b[2];
  for (int tries = 0; tries < 3; tries++) {
    if (scsWrite(id, 48, &unlock, 1) != 1 && scsWrite(newId, 48, &unlock, 1) != 1) continue;
    delay(20);
    scsWrite(id, 5, &newId, 1);                       // the reply may come from the new ID: don't trust it
    delay(100);                                       // let the EEPROM finish before locking
    if (scsWrite(newId, 48, &lock, 1) != 1) continue;
    delay(20);
    if (scsRead(newId, 48, b, 1) == 1 && b[0] == 1 && scsRead(newId, 5, b, 1) == 1 && b[0] == newId) return true;
  }
  return false;
}
static void echoCheck() {   // ping an unused ID: our own bytes come back through the 1 kOhm join
  uint8_t pkt[6] = {0xFF, 0xFF, 0xFD, 2, 0x01}, buf[6], got = 0;
  pkt[5] = scsSum(pkt + 2, 3);
  while (bus.available()) bus.read();
  bus.write(pkt, 6);
  bus.flush();
  for (uint32_t t0 = micros(); got < 6 && micros() - t0 < 1500;) {
    int c = bus.read();
    if (c >= 0) buf[got++] = c;
  }
  echo = got == 6 && !memcmp(buf, pkt, 6);
}

// What each servo-line fault means: {fault, screen hint, console explanation}
static const char* const WHY[4][3] = {
  {"HELD HIGH", "unplug servos + RESET",
   "GPIO19 stays HIGH while GPIO20 pulls it LOW through the 1 kOhm: something drives it high.\n"
   "  Unplug the 3 servo signal wires and press RESET. If that clears it, the servos'\n"
   "  pull-ups beat the 1 kOhm: use 330-470 Ohm instead."},
  {"HELD LOW", "battery ON? see console",
   "GPIO19 stays LOW while GPIO20 pulls it HIGH through the 1 kOhm: something holds it low.\n"
   "  A servo with no power clamps the line (battery switch ON? servo VIN on the 5 V rail?),\n"
   "  or a short to GND."},
  {"OPEN", "check 20-1k-SIGNAL-19",
   "GPIO19 doesn't follow GPIO20 at all: the GPIO20 -> 1 kOhm -> SIGNAL point <- GPIO19 path is open.\n"
   "  Check the resistor and both wires (Xelulu_Wiring.md section 10)."},
  {"TOO SLOW", "shorter wires, 1 kOhm?",
   "The line follows when slow, but our bytes don't come back at 1 Mbps.\n"
   "  Shorter servo wires; check the resistor is 1 kOhm (brown-black-red), not 10 kOhm."},
};
static const char* why(uint8_t col) {   // col 1 = screen hint, 2 = console explanation
  for (auto& w : WHY)
    if (lineFault && !strcmp(lineFault, w[0])) return w[col];
  return "";
}
// The line itself, before any UART takes the pins: TX drives it through the 1 kOhm and
// RX reads the SIGNAL point against a weak pull the other way. nullptr = it follows both ways.
static const char* lineTest() {
  pinMode(PIN_SERVO_TX, OUTPUT);
  digitalWrite(PIN_SERVO_TX, HIGH);
  pinMode(PIN_SERVO_RX, INPUT_PULLDOWN);
  delay(2);
  bool up = digitalRead(PIN_SERVO_RX);
  digitalWrite(PIN_SERVO_TX, LOW);
  pinMode(PIN_SERVO_RX, INPUT_PULLUP);
  delay(2);
  bool down = !digitalRead(PIN_SERVO_RX);
  digitalWrite(PIN_SERVO_TX, HIGH);   // back to the UART's idle level
  if (up && down) return nullptr;
  if (up) return "HELD HIGH";
  if (down) return "HELD LOW";
  return "OPEN";
}
static void servoBegin() {
  lineFault = lineTest();
  if (!lineFault) {
    bus.setRxBufferSize(256);
    bus.begin(1000000, SERIAL_8N1, PIN_SERVO_RX, PIN_SERVO_TX);
    echoCheck();
    if (!echo) {   // it follows when slow, not at 1 Mbps
      bus.end();
      lineFault = "TOO SLOW";
    }
  }
  if (lineFault) Serial0.printf("[servo] SERVO LINE %s: the servo steps are skipped.\n  %s\n", lineFault, why(2));
  else Serial0.println("[servo] line OK: our bytes echo back through the 1 kOhm");
}

// ============================================================ live: screen rows, console line
static const char* mmStr(uint8_t ch, char* b) {   // "--" = sensor not found, "far" = nothing in range
  if (!tofUp[ch]) return "--";
  if (tofMm[ch] >= 2000) return "far";
  snprintf(b, 8, "%u", tofMm[ch]);
  return b;
}
static void drawPads() {   // a box per pad, green while touched
  strip.fillScreen(C_BLACK);
  strip.setTextSize(2);
  for (uint8_t e = 0; e < 5; e++) {
    bool on = touchUp && (padBits >> e & 1);
    strip.fillRoundRect(e * 64, 0, 62, 20, 4, !touchUp ? 0x8000 : on ? C_GREEN : C_GREY);
    strip.setTextColor(on ? C_BLACK : C_WHITE);
    strip.setCursor(e * 64 + (62 - 12 * (int)strlen(PAD[e])) / 2, 2);
    strip.print(PAD[e]);
  }
  pushStrip(86);
}
static char audioRes[28];   // the last audio verdict, kept on screen
static bool audioOk;
static void drawMics() {    // a level bar per mic, -90..-20 dBFS; dark red = nobody drives that slot
  strip.fillScreen(C_BLACK);
  strip.setTextSize(2);
  strip.setTextColor(C_WHITE);
  for (uint8_t c = 0; c < 2; c++) {
    int x = c * 160;
    strip.setCursor(x + 2, 2);
    strip.print(c ? "R" : "L");
    strip.drawRect(x + 16, 1, 140, 18, C_GREY);
    if (!micUp || micFlat[c]) strip.fillRect(x + 18, 3, 136, 14, 0x8000);
    else strip.fillRect(x + 18, 3, constrain((int)((micDb[c] + 90) * 136 / 70), 0, 136), 14, C_GREEN);
  }
  pushStrip(188);
}
static void drawRow(uint8_t i) {   // one row per call keeps every call short
  char a[8], b[8];
  switch (i) {
    case 0: row(64, C_WHITE, "ToF mm  L %4s   R %4s", mmStr(0, a), mmStr(1, b)); break;
    case 1: drawPads(); break;
    case 2:
      if (imuUp) row(108, C_WHITE, "Gyro  x%+4.0f y%+4.0f z%+4.0f", gyro[0], gyro[1], gyro[2]);
      else row(108, C_RED, "GY-87 not found");
      break;
    case 3:
      if (imuUp) row(128, C_WHITE, "Acc   x%+5.2f y%+5.2f z%+5.2f", acc[0], acc[1], acc[2]);
      else row(128, C_BLACK, "");
      break;
    case 4: row(148, C_WHITE, "Enc   L%+7d  R%+7d", encCount[0], encCount[1]); break;
    case 5: {
      if (lineFault) {
        row(168, C_RED, "SERVO LINE %s", lineFault);
        break;
      }
      char p[3][8];
      uint8_t v = 0;
      for (uint8_t id = 1; id <= 3; id++) {
        if (servoOk[id]) {
          snprintf(p[id - 1], sizeof p[0], "%d", servo[id].pos);
          v = max(v, servo[id].volt10);
        } else strcpy(p[id - 1], "--");
      }
      row(168, C_WHITE, v ? "Srv H%4s L%4s R%4s %.1fV" : "Srv H%4s L%4s R%4s", p[0], p[1], p[2], v / 10.0f);
      break;
    }
    case 6: drawMics(); break;
    case 7:
      if (*audioRes) row(208, audioOk ? C_GREEN : C_RED, "%s", audioRes);
      else row(208, C_RED, "%s", why(1));
      break;
  }
}
static void printLive() {
  char a[8], b[8], pads[32] = "";
  for (uint8_t e = 0; e < 5; e++)
    if (padBits >> e & 1) { strlcat(pads, PAD[e], sizeof pads); strlcat(pads, " ", sizeof pads); }
  Serial0.printf("ToF L %s R %s | touch %s", mmStr(0, a), mmStr(1, b), !touchUp ? "MISSING " : *pads ? pads : "- ");
  if (touchUp) Serial0.printf("(sig %d %d %d %d %d) ", padSignal(0), padSignal(1), padSignal(2), padSignal(3), padSignal(4));
  if (imuUp) Serial0.printf("| gyro %+.0f %+.0f %+.0f acc %+.2f %+.2f %+.2f ", gyro[0], gyro[1], gyro[2], acc[0], acc[1], acc[2]);
  Serial0.printf("| enc %d %d", encCount[0], encCount[1]);
  if (micUp) Serial0.printf(" | mic L %s R %s", micFlat[0] ? "--" : String(micDb[0], 0).c_str(),
                            micFlat[1] ? "--" : String(micDb[1], 0).c_str());
  if (lineFault) {
    Serial0.printf(" | servo line %s\n", lineFault);
    return;
  }
  Serial0.print(" | servo");
  for (uint8_t id = 1; id <= 3; id++)
    if (servoOk[id]) Serial0.printf(" %u:%d %.1fV", id, servo[id].pos, servo[id].volt10 / 10.0f);
    else Serial0.printf(" %u:--", id);
  Serial0.println();
}
static void liveTick() {
  static uint32_t readAt, servoAt, rowAt, printAt;
  static uint8_t next = 0;
  uint32_t now = millis();
  if (now - readAt >= 20) { readAt = now; readSensors(); }
  if (!lineFault && now - servoAt >= 300) {
    servoAt = now;
    for (uint8_t id = 1; id <= 3; id++) servoOk[id] = scsFb(id, servo[id]);
  }
  if (now - rowAt >= 25) { rowAt = now; drawRow(next); next = (next + 1) % 8; }
  if (now - printAt >= 500) { printAt = now; printLive(); }
}
static void wait(uint32_t ms) {   // a delay that keeps the live readings going
  for (uint32_t t0 = millis(); millis() - t0 < ms;) {
    liveTick();
    delay(1);
  }
}
static void live() { status("LIVE readings", "touch / tilt / cover"); }

// ============================================================ wheels: TB6612 + N20 quadrature encoders
static void wheelsBegin() {
  for (uint8_t m = 0; m < 2; m++) {
    ledcAttach(MOT_PWM[m], 20000, 10);    // 20 kHz, as xelulu_fw
    pcnt_unit_config_t uc = {};
    uc.low_limit = -30000;
    uc.high_limit = 30000;
    uc.flags.accum_count = 1;             // keeps counting past the limits (watch points below)
    pcnt_new_unit(&uc, &enc[m]);
    pcnt_glitch_filter_config_t gf = {.max_glitch_ns = 1000};
    pcnt_unit_set_glitch_filter(enc[m], &gf);
    // x4 quadrature, as the IDF rotary-encoder example: each channel's edges, signed by the other's level
    pcnt_chan_config_t ca = {}, cb = {};
    ca.edge_gpio_num = ENC_C1[m];
    ca.level_gpio_num = ENC_C2[m];
    cb.edge_gpio_num = ENC_C2[m];
    cb.level_gpio_num = ENC_C1[m];
    pcnt_channel_handle_t a, b;
    pcnt_new_channel(enc[m], &ca, &a);
    pcnt_new_channel(enc[m], &cb, &b);
    pcnt_channel_set_edge_action(a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_channel_set_edge_action(b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_unit_add_watch_point(enc[m], 30000);
    pcnt_unit_add_watch_point(enc[m], -30000);
    pcnt_unit_enable(enc[m]);
    pcnt_unit_clear_count(enc[m]);
    pcnt_unit_start(enc[m]);
  }
}
static void motor(uint8_t m, float d) {   // d -1..1, + = IN1 high
  digitalWrite(MOT_IN1[m], d > 0);
  digitalWrite(MOT_IN2[m], d < 0);
  ledcWrite(MOT_PWM[m], (uint32_t)(fabsf(d) * 1023));
}
static void motorsStop() {   // TB6612 short brake, then all pins low (STBY is tied high)
  for (uint8_t m = 0; m < 2; m++) {
    digitalWrite(MOT_IN1[m], HIGH);
    digitalWrite(MOT_IN2[m], HIGH);
    ledcWrite(MOT_PWM[m], 1023);
  }
  wait(100);
  for (uint8_t m = 0; m < 2; m++) motor(m, 0);
}
static void spinWheels() {   // left N20 forward, back; then the right one. Each run reports its encoder counts.
  char l1[28], l2[28];
  for (int s = 5; s > 0; s--) {
    snprintf(l1, sizeof l1, "Wheels spin in %d s", s);
    status(l1, "lift the robot!");
    wait(1000);
  }
  static const char* const NAME[2] = {"LEFT N20", "RIGHT N20"};
  for (uint8_t m = 0; m < 2; m++)
    for (uint8_t d = 0; d < 2; d++) {
      int before[2], after[2];
      for (uint8_t w = 0; w < 2; w++) pcnt_unit_get_count(enc[w], &before[w]);
      snprintf(l1, sizeof l1, "%s %s", NAME[m], d ? "backward" : "forward");
      status(l1, "");
      for (int i = 1; i <= 30; i++) {   // 0.3 s soft start
        motor(m, (d ? -WHEEL_DUTY : WHEEL_DUTY) * i / 30);
        wait(10);
      }
      wait(1500);
      motorsStop();
      for (uint8_t w = 0; w < 2; w++) pcnt_unit_get_count(enc[w], &after[w]);
      int own = after[m] - before[m];
      snprintf(l2, sizeof l2, "encoder %+d counts", own);
      status(l1, l2);
      Serial0.printf("   %s: encoder L %+d  R %+d%s\n", l1, after[0] - before[0], after[1] - before[1],
                  own ? "" : "   <- none on its own encoder: did the wheel turn?");
      wait(1000);
    }
}

// ============================================================ servos: IDs, then one after the other
// Every ID: found[] = those that answered cleanly. True if any answer was garbled: two
// servos sharing an ID talk over each other. ~0.8 s, the live readings keep going.
static int clashId = -1;   // the last ID that garbled, for the screen
static bool scanBus(bool* found) {
  echoCheck();
  bool clash = false;
  for (int id = 0; id < 254; id++) {
    uint8_t b[2];
    int k = scsRead(id, 56, b, 2);   // a read, not a ping: same-ID twins answer different positions, so they garble
    for (int r = 0; r < 2 && k == 0; r++) k = scsRead(id, 56, b, 2);   // twins garble every read, a plug-in glitch only one
    found[id] = k == 1;
    if (k == 0) clashId = id;
    clash |= k == 0;
    if (id % 8 == 7) liveTick();
  }
  return clash;
}
static int answering(const bool* found) {
  int n = 0;
  for (int k = 0; k < 254; k++) n += found[k];
  return n;
}
static bool idsOk() {
  uint8_t b[2];
  for (auto& j : ORDER)
    if (scsRead(j.id, 56, b, 2) != 1) return false;
  return true;
}
static bool gaveUp(uint32_t t0) {   // a step waiting on you: WAIT_MS, or `x` on the console
  bool x = false;
  for (int c; (c = key()) >= 0;) x |= c == 'x';
  return x || millis() - t0 > WAIT_MS;
}

// The bus can't tell two servos with one ID apart, and new SC09s are all ID 1, so each
// gets its ID alone: unplug them all, then plug them in one at a time when asked.
static bool setIds(bool force) {
  if (!force && idsOk()) {
    status("Servo IDs already set", "1 head 2 L-arm 3 R-arm");
    wait(1500);
    return true;
  }
  static bool found[254];
  bool done[254] = {};
  status("Unplug the signal", "wire of ALL servos");
  for (uint32_t t0 = millis(); scanBus(found) || answering(found);)
    if (gaveUp(t0)) return false;
  for (auto& j : ORDER) {
    char l1[28], l2[28];
    bool alone = false;   // after a clash: the bus was emptied, then only this one went in
    int fresh = -1;
    snprintf(l2, sizeof l2, "%s servo", j.name);
    status("Plug in the", l2);
    for (uint32_t t0 = millis(); fresh < 0;) {
      if (gaveUp(t0)) return false;
      if (scanBus(found)) {   // it shares an ID with one already done
        char l3[28];
        snprintf(l3, sizeof l3, "ID %d: unplug ALL servos", clashId);
        status("Two servos, same ID:", l3);
        for (uint32_t t1 = millis(); scanBus(found) || answering(found);)
          if (gaveUp(t1)) return false;
        status("Plug in ONLY the", l2);
        alone = true;
        t0 = millis();
        continue;
      }
      int n = 0, id = -1;
      for (int k = 0; k < 254; k++)
        if (found[k] && (alone || !done[k])) { n++; id = k; }
      if (n == 1) fresh = id;
      else if (n > 1) status("Plug in only ONE", "new servo at a time");
    }
    if (fresh != j.id && !scsSetId(fresh, j.id)) {
      status(l2, "ID write FAILED");
      wait(2000);
      return false;
    }
    done[j.id] = true;
    snprintf(l1, sizeof l1, "%s = ID %u", j.name, j.id);
    status(l1, "");
    wait(1200);
  }
  // Power-cycle check: a servo that kept the ID only in RAM comes back with its old ID.
  status("Saved? UNPLUG all 3", "servos, then plug back");
  for (uint32_t t0 = millis(); scanBus(found) || answering(found);)
    if (gaveUp(t0)) return false;
  status("Now plug all 3", "servos back in");
  for (uint32_t t0 = millis(); !idsOk(); wait(300)) {
    if (scanBus(found)) {   // a clash after power-up: the save didn't hold
      char l2[28];
      snprintf(l2, sizeof l2, "ID %d clash after replug", clashId);
      status("IDs NOT SAVED!", l2);
      Serial0.println("   IDs did not survive power-off: the EEPROM write didn't take. Battery low? Retry with `i`.");
      wait(4000);
      return false;
    }
    if (gaveUp(t0)) return false;
  }
  status("IDs SAVED in servos:", "1 head 2 L-arm 3 R-arm");
  wait(2000);
  return true;
}

static void moveServos() {   // left arm, right arm, head: torque on where each IS, swing, back, limp
  for (auto& j : ORDER) {
    char l2[28];
    Fb f;
    if (!scsFb(j.id, f)) {
      status(j.name, "servo not answering");
      wait(1500);
      continue;
    }
    if (f.volt10 > 62) {   // SC09 is a 4-6 V part
      snprintf(l2, sizeof l2, "%.1f V! needs 5 V rail", f.volt10 / 10.0f);
      status(j.name, l2);
      wait(3000);
      continue;
    }
    snprintf(l2, sizeof l2, "servo (ID %u) moving", j.id);
    status(j.name, l2);
    const int a = SERVO_SWING_DEG * 1024 / 300;   // SC09: 1024 counts over 300 deg
    const int path[3] = {f.pos + a, f.pos - a, f.pos};
    int at = f.pos;
    scsGoal(j.id, f.pos);
    scsTorque(j.id, true);
    for (int target : path) {
      for (int i = 1; i <= 40; i++) {   // 0.4 s, eased in and out
        scsGoal(j.id, at + (int)((target - at) * (1 - cosf(PI * i / 40)) / 2));
        wait(10);
      }
      at = target;
      wait(300);
    }
    scsTorque(j.id, false);
  }
}

// ============================================================ audio step: mics alive, speaker heard, stereo sides
// Tap one mic for 6 s: the loudest moment must be TAP_DB over the room, and SIDE_DB louder on
// the tapped side than on the other. 1 = right side, 0 = wrong side, -1 = nothing / can't tell.
static int tapTest(uint8_t side) {
  static const char* const SIDE[2] = {"LEFT", "RIGHT"};
  char l1[28], l2[28];
  snprintf(l1, sizeof l1, "Tap the %s mic", SIDE[side]);
  status(l1, "(scratch / blow on it)");
  micTake();
  wait(800);
  MicSum q = micTake();
  float room[2] = {db(q.pow[0] / q.n), db(q.pow[1] / q.n)}, rise = 0, diff = 0;
  for (uint32_t t0 = millis(); millis() - t0 < 6000;) {
    wait(100);
    MicSum w = micTake();
    float lv[2] = {db(w.pow[0] / w.n), db(w.pow[1] / w.n)};
    float r = max(lv[0] - room[0], lv[1] - room[1]);
    if (r > rise) { rise = r; diff = lv[side] - lv[1 - side]; }
  }
  int res = rise < TAP_DB ? -1 : diff >= SIDE_DB ? 1 : diff <= -SIDE_DB ? 0 : -1;
  if (rise < TAP_DB) snprintf(l2, sizeof l2, "no tap heard");
  else snprintf(l2, sizeof l2, res == 1 ? "OK: %+.0f dB louder" : res == 0 ? "WRONG side! %+.0f dB" : "same both sides %+.0f", diff);
  status(l1, l2);
  Serial0.printf("   %s: loudest %+.0f dB over the room, tapped side %+.0f dB vs the other\n", l1, rise, diff);
  wait(2000);
  return res;
}
static void runAudio() {
  char l1[28], l2[28];
  if (!micUp || !spkUp) {
    status("Audio: I2S start FAILED", micUp ? "speaker port" : "mic port");
    strcpy(audioRes, "Audio: I2S FAILED");
    audioOk = false;
    wait(2000);
    return;
  }
  // 1. both mics alive: each slot must carry a changing signal, at a sane level
  toneHz = 0;
  status("Mics: listening...", "quiet please");
  wait(300);
  micTake();
  wait(1000);
  MicSum q = micTake();
  bool alive[2];
  for (int c = 0; c < 2; c++) {
    float lv = db(q.pow[c] / q.n);
    alive[c] = q.flat[c] < q.n / 2 && lv < -20;   // flat = nobody on that slot; very loud = two mics fighting
    Serial0.printf("   mic %s: room %.0f dBFS, %s\n", c ? "R" : "L", lv, alive[c] ? "alive" : "DEAD / garbled");
  }
  snprintf(l1, sizeof l1, "Mic L %s  R %s", alive[0] ? "ok" : "DEAD", alive[1] ? "ok" : "DEAD");
  status(l1, alive[0] && alive[1] ? "" : "check SEL + DOUT wires");
  wait(2000);
  // 2. the chime: three notes you should hear
  status("Speaker: 3 notes", "listen...");
  for (float f : {523.f, 659.f, 784.f}) { toneHz = f; wait(250); }
  toneHz = 0;
  wait(400);
  // 3. loopback: the mics must hear the TONE_HZ tone: amp + speaker + mics in one go
  status("Speaker: 1 kHz tone", "the mics listen...");
  micTake();
  wait(500);
  MicSum s = micTake();
  toneHz = TONE_HZ;
  wait(200);
  micTake();
  wait(1000);
  MicSum t = micTake();
  toneHz = 0;
  float gain[2];
  for (int c = 0; c < 2; c++) gain[c] = db(t.tone[c] / t.n) - db(s.tone[c] / s.n);
  bool heard = (alive[0] && gain[0] >= HEARD_DB) || (alive[1] && gain[1] >= HEARD_DB);
  snprintf(l1, sizeof l1, "Tone heard L%+.0f R%+.0f dB", gain[0], gain[1]);
  status(l1, heard ? "speaker + amp OK" : alive[0] || alive[1] ? "NOT heard: amp/speaker?" : "no mic: did you hear?");
  Serial0.printf("   1 kHz tone over silence: L %+.0f dB  R %+.0f dB -> %s\n", gain[0], gain[1], heard ? "heard" : "NOT heard");
  wait(2500);
  // 4. stereo: tap each mic, the tapped side must be the louder one
  int tl = tapTest(0), tr = tapTest(1);
  bool stereo = tl == 1 && tr == 1;
  const char* lr = stereo ? "ok" : tl == 0 && tr == 0 ? "SWAP" : "BAD";
  snprintf(audioRes, sizeof audioRes, "Mic %s%s Spk %s L/R %s", alive[0] ? "L" : "-", alive[1] ? "R" : "-",
           heard ? "ok" : "BAD", lr);
  audioOk = alive[0] && alive[1] && heard && stereo;
  status(audioOk ? "AUDIO ALL OK" : "AUDIO PROBLEM", audioRes);
  Serial0.printf("[audio] %s%s\n", audioRes, stereo || !(tl == 0 && tr == 0) ? "" : "  <- L/R swapped: swap the mics' SEL wires");
  wait(3000);
}

static void runAll() {
  if (lineFault) {
    status("Servo steps skipped:", "servo line problem");
    wait(2500);
  } else if (setIds(false)) moveServos();
  else {
    status("Servo step skipped", "");
    wait(2000);
  }
  spinWheels();
  runAudio();
  live();
}

void setup() {
  // FIRST: TB6612 STBY is tied high, so these six pins low are what keeps the wheels still
  for (uint8_t m = 0; m < 2; m++)
    for (uint8_t p : {MOT_PWM[m], MOT_IN1[m], MOT_IN2[m]}) {
      pinMode(p, OUTPUT);
      digitalWrite(p, LOW);
    }
  Serial0.begin(115200);   // the console: the COM port (CH340, UART0 = GPIO43/44)
  Serial0.println("\nXelulu test bench starting");
  tftBegin();
  row(0, C_CYAN, "XELULU TEST BENCH");
  servoBegin();
  status("Starting...", "");
  sensorsBegin();
  wheelsBegin();
  audioBegin();
  runAll();
}

void loop() {
  liveTick();
  int c = key();
  if (c == 'a') runAll();
  else if ((c == 'i' || c == 's') && lineFault) Serial0.printf("servo line %s: fix it, then press RESET\n", lineFault);
  else if (c == 'i') { if (setIds(true)) moveServos(); live(); }
  else if (c == 's') { moveServos(); live(); }
  else if (c == 'w') { spinWheels(); live(); }
  else if (c == 'm') { runAudio(); live(); }
  delay(1);
}
