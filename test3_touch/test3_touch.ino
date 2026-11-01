// TEST 3 - touch calibration for the HEAD, BELLY and BACK pads (MPR121), the Eillik way:
// each pad is measured 5 s TOUCHED, then 5 s UNTOUCHED, and the two averages give its
// threshold: halfway between them, with the release threshold halfway again (hysteresis).
// The untouched noise is checked too: the trip has to sit well above it (Eillik/debug.md 4b).
// Follow the screen, one pad after the other:
//   "Get ready to TOUCH it"  put a hand on that pad, through the casing, the way a person
//                            pats the robot (not a hard press)
//   "TOUCH it now, hold on"  keep it there until the bar is full (5 s)
//   "Let go", then "Hands OFF everything": touch nothing until the bar is full (5 s)
// At the end: the averages and thresholds per pad, the lines for xelulu_fw, and a live check
// with the new thresholds (a pad turns green while touched). BOOT, or `c` on the console =
// calibrate again.
// It averages the chip's raw filtered data, not baseline - data: the chip's baseline follows a
// touch that is under its current threshold, which would shrink the touched average.
// Wiring per Xelulu_Wiring.md: MPR121 on I2C SDA 8 / SCL 9, ELE0 head, ELE1 back, ELE2 belly.
// Battery ON. Console = Serial (native USB COM7), 115200.
// Board settings / libraries = xelulu_testbench's (Adafruit GFX + Adafruit ST7735 and ST7789).
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#define PIN_I2C_SDA   8
#define PIN_I2C_SCL   9
#define PIN_TFT_SCLK 12
#define PIN_TFT_MOSI 11
#define PIN_TFT_CS   10
#define PIN_TFT_DC   14
#define PIN_TFT_RST  21
#define PIN_TFT_EN   45    // backlight, HIGH = on
#define PIN_BOOT      0    // BOOT button, LOW = pressed
#define MPR        0x5A
#define WINDOW_MS  5000    // each touched / untouched measurement
#define READY_S       3    // countdown before touching: the hand is in place when the window starts
#define LETGO_S       2    // hand away before the untouched window
static const uint8_t MOTOR_PINS[] = {1, 2, 42, 41, 13, 47};   // TB6612 STBY is tied high: keep these LOW
static const char* const PAD[3] = {"HEAD", "BACK", "BELLY"};   // ELE0-2 = xelulu_fw zones 0-2
static const uint8_t ORDER[3] = {0, 2, 1};                      // head, belly, back

struct Avg { float mean[3]; uint16_t lo[3]; };   // per pad: average and lowest filtered data in one window

static Adafruit_ST7789 tft(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
static bool up;
static uint8_t thr[5] = {6, 6, 6, 6, 6}, rel[5] = {3, 3, 3, 3, 3};   // xelulu_fw Config.h defaults; the hands keep them
static Avg touched[3], untouched[3];   // by the pad being calibrated in that window
static uint8_t shownOn;                // pads ON in the live check, to log changes

// ---- screen ----
static void tftBegin() {   // Display_test_code's sequence
  delay(500);
  pinMode(PIN_TFT_EN, OUTPUT);
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
  tft.setRotation(1);   // landscape, 320x240
  tft.fillScreen(ST77XX_BLACK);
}
// One padded line of text over a black background: redraws without flicker.
static void text(int x, int y, uint8_t size, uint16_t color, const char* fmt, ...) {
  char b[56];
  va_list a;
  va_start(a, fmt);
  vsnprintf(b, sizeof b, fmt, a);
  va_end(a);
  tft.setTextSize(size);
  tft.setTextColor(color, ST77XX_BLACK);
  tft.setCursor(x, y);
  tft.printf("%-*s", (320 - x) / (6 * size), b);
}

// ---- MPR121: xelulu_fw sense/Touch.cpp's setup ----
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
static void applyThresholds() {   // stop, write, run again: the chip re-learns its baselines (hands off)
  i2cWrite(MPR, 0x5E, 0x00);
  for (uint8_t e = 0; e < 5; e++) {
    i2cWrite(MPR, 0x41 + 2 * e, thr[e]);
    i2cWrite(MPR, 0x42 + 2 * e, rel[e]);
  }
  i2cWrite(MPR, 0x5E, 0x85);   // run ELE0-4 as xelulu_fw does, baseline from the first reading
}
static bool touchBegin() {
  i2cWrite(MPR, 0x80, 0x63);   // soft reset
  delay(1);
  i2cWrite(MPR, 0x5E, 0x00);   // stop mode: config is writable only here
  uint8_t c2 = 0;
  if (!i2cRead(MPR, 0x5D, &c2, 1) || c2 != 0x24) return false;
  static const uint8_t REGS[][2] = {
    {0x2B, 0x01}, {0x2C, 0x01}, {0x2D, 0x0E}, {0x2E, 0x00}, {0x2F, 0x01}, {0x30, 0x01},   // slow falling
    {0x31, 0xFF}, {0x32, 0x02}, {0x33, 0x00}, {0x34, 0x00}, {0x35, 0x00}, {0x5B, 0x00},   // filter, = Touch.cpp
    {0x5C, 0x10}, {0x5D, 0x20}, {0x7B, 0x0B}, {0x7D, 200}, {0x7E, 130}, {0x7F, 180},
  };
  for (auto& r : REGS) i2cWrite(MPR, r[0], r[1]);
  applyThresholds();
  return true;
}
static int padSignal(uint8_t e) {    // baseline - filtered: how strongly ELEe is touched
  uint8_t f[2], b;
  if (!i2cRead(MPR, 0x04 + 2 * e, f, 2) || !i2cRead(MPR, 0x1E + e, &b, 1)) return 0;
  return (b << 2) - (f[0] | (f[1] & 3) << 8);
}

// ---- calibration ----
// Average ELE0-2's filtered data over one window, with a progress bar.
static Avg measure(uint16_t color) {
  Avg a;
  double sum[3] = {};
  uint32_t n = 0;
  int drawn = 0;
  for (int e = 0; e < 3; e++) a.lo[e] = 1023;
  tft.drawRect(4, 150, 312, 24, ST77XX_WHITE);
  for (uint32_t t0 = millis(), el; (el = millis() - t0) < WINDOW_MS; delay(10)) {
    uint8_t b[6];
    if (i2cRead(MPR, 0x04, b, 6)) {   // filtered data ELE0-2, 10 bits each
      for (int e = 0; e < 3; e++) {
        uint16_t d = b[2 * e] | (b[2 * e + 1] & 3) << 8;
        sum[e] += d;
        a.lo[e] = min(a.lo[e], d);
      }
      n++;
    }
    int w = 310 * el / WINDOW_MS;
    if (w > drawn) {
      tft.fillRect(5 + drawn, 151, w - drawn, 22, color);
      drawn = w;
    }
  }
  for (int e = 0; e < 3; e++) a.mean[e] = n ? sum[e] / n : 0;
  return a;
}
static void phase(uint8_t p, uint16_t color, const char* what) {
  tft.fillRect(0, 26, 320, 214, ST77XX_BLACK);
  text(4, 40, 4, ST77XX_WHITE, "%s", PAD[p]);
  text(4, 90, 2, color, "%s", what);
  Serial.printf("[cal] %s: %s\n", PAD[p], what);
}
static void countdown(int s) {
  for (; s > 0; s--) {
    text(4, 140, 4, ST77XX_YELLOW, "%d", s);
    delay(1000);
  }
}

static void results() {
  tft.fillRect(0, 26, 320, 214, ST77XX_BLACK);
  uint8_t oor = 0;
  i2cRead(MPR, 0x02, &oor, 1);   // out of range: the chip couldn't tune that pad
  bool weak = false, none = false;
  text(4, 28, 1, ST77XX_WHITE, "PAD         NO-TOUCH/TOUCH avg  THR");   // columns line up with the size-2 rows
  for (int i = 0; i < 3; i++) {
    uint8_t p = ORDER[i];
    float u = untouched[p].mean[p], t = touched[p].mean[p];
    int depth = lroundf(u - t), noise = ceilf(u - untouched[p].lo[p]);   // touch depth; worst dip with nobody touching
    int th = constrain((int)lroundf((u - t) / 2), 2, 255);               // halfway between the two averages
    const char* v = oor >> p & 1 ? "OOR" : depth < 2 ? "NONE" : depth < 4 || th <= 2 * noise ? "WEAK" : "OK";
    if (depth >= 2 && !(oor >> p & 1)) {   // NONE / OOR keep the old threshold
      thr[p] = th;
      rel[p] = max(1, th / 2);
    }
    weak |= !strcmp(v, "WEAK");
    none |= depth < 2 || oor >> p & 1;
    text(4, 40 + i * 22, 2, !strcmp(v, "OK") ? ST77XX_GREEN : !strcmp(v, "WEAK") ? ST77XX_YELLOW : ST77XX_RED,
         "%-5s %4.0f/%-4.0f %3u %s", PAD[p], u, t, thr[p], v);
    Serial.printf("[cal] %-5s no-touch avg %.1f, touch avg %.1f -> touch depth %d, noise %d -> thr %u, rel %u  %s\n",
                  PAD[p], u, t, depth, noise, thr[p], rel[p], v);
  }
  // A hand on one pad can pull a neighbour down too: past that pad's threshold, one touch fires both.
  for (uint8_t p = 0; p < 3; p++)
    for (uint8_t q = 0; q < 3; q++) {
      int x = lroundf(untouched[q].mean[q] - touched[p].mean[q]);
      if (q == p || x < thr[q]) continue;
      text(4, 114, 1, ST77XX_ORANGE, "! touching %s also trips %s (%d >= %u)", PAD[p], PAD[q], x, thr[q]);
      Serial.printf("[cal] touching %s also pulls %s down by %d, over its threshold %u: it fires both\n", PAD[p],
                    PAD[q], x, thr[q]);
    }
  text(4, 104, 1, ST77XX_CYAN, "touchThr {%u,%u,%u,%u,%u}  relThr {%u,%u,%u,%u,%u}", thr[0], thr[1], thr[2], thr[3],
       thr[4], rel[0], rel[1], rel[2], rel[3], rel[4]);
  Serial.printf("\nxelulu_fw/Config.h (head, back, belly, L hand, R hand):\n"
                "  uint8_t  touchThr[ZONES] = {%u, %u, %u, %u, %u};\n"
                "  uint8_t  relThr[ZONES]   = {%u, %u, %u, %u, %u};\n"
                "or on the xelulu_fw console:\n"
                "  cal set touchThr %u %u %u %u %u\n  cal set relThr %u %u %u %u %u\n  cal save\n\n",
                thr[0], thr[1], thr[2], thr[3], thr[4], rel[0], rel[1], rel[2], rel[3], rel[4],
                thr[0], thr[1], thr[2], thr[3], thr[4], rel[0], rel[1], rel[2], rel[3], rel[4]);
  if (weak) Serial.println("WEAK: the touch barely clears the untouched noise. A thinner wall there, a bigger pad, or copper\n"
                           "  foil flush against the inside of the shell (no air gap) helps (Eillik/debug.md 4b).");
  if (none) Serial.println("NONE / OOR: that pad's reading didn't move, or the chip couldn't tune it. Check its wire to\n"
                           "  ELE0-2. It keeps its old threshold.");
  applyThresholds();
  shownOn = 0;
  text(4, 128, 2, ST77XX_CYAN, "LIVE  signal / threshold");
  text(4, 228, 1, ST77XX_CYAN, "BOOT or c on the console = calibrate again");
}

static void calibrate() {
  for (uint8_t p : ORDER) {
    phase(p, ST77XX_YELLOW, "Get ready to TOUCH it");
    text(4, 114, 1, ST77XX_WHITE, "one hand, through the casing, like a pat");
    countdown(READY_S);
    phase(p, ST77XX_GREEN, "TOUCH it now, hold on");
    touched[p] = measure(ST77XX_GREEN);
    phase(p, ST77XX_YELLOW, "Let go");
    countdown(LETGO_S);
    phase(p, ST77XX_CYAN, "Hands OFF everything");
    untouched[p] = measure(ST77XX_CYAN);
  }
  results();
}

void setup() {
  for (uint8_t p : MOTOR_PINS) {   // FIRST: wheels still
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
  pinMode(PIN_BOOT, INPUT_PULLUP);
  Serial.begin(115200);
  tftBegin();
  text(4, 4, 2, ST77XX_CYAN, "TEST 3  TOUCH CALIBRATION");
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  Wire.setTimeOut(25);
  text(4, 60, 2, ST77XX_YELLOW, "Hands OFF the robot");
  delay(2000);   // the chip tunes itself and learns its baselines at start: nobody touching
  up = touchBegin();
  if (!up) {
    text(4, 60, 2, ST77XX_RED, "MPR121 not found (0x5A)");
    Serial.println("[touch] MPR121 not answering at 0x5A: SDA 8 / SCL 9, 3.3 V, battery ON?");
    return;
  }
  delay(500);
  calibrate();
}

void loop() {
  if (!up) return;
  uint8_t st = 0;
  i2cRead(MPR, 0x00, &st, 1);   // touch status ELE0-7, with the new thresholds
  for (int i = 0; i < 3; i++) {
    uint8_t p = ORDER[i];
    bool on = st >> p & 1;
    text(4, 152 + i * 24, 2, on ? ST77XX_GREEN : ST77XX_WHITE, "%-5s %4d / %-3u %s", PAD[p], padSignal(p), thr[p],
         on ? "ON" : "");
    if (on != (shownOn >> p & 1)) Serial.printf("[live] %s %s\n", PAD[p], on ? "ON" : "off");
  }
  shownOn = st & 7;
  if (digitalRead(PIN_BOOT) == LOW || Serial.read() == 'c') calibrate();
  delay(30);
}
