// TEST 7 - servo spin: plug in ONE SC09 (any ID), it moves +90 deg (front), 0, -90 deg (back), 0,
// forever. 0 deg = 512 counts = mid-travel. Unplug it and plug in another any time: it rescans.
// ID and angle show on the TFT. Wiring per Xelulu_Wiring.md section 10:
// GPIO20 -> 1 kOhm -> SIGNAL <- GPIO19. Battery ON (servo VIN is the 5 V rail).
// Console = Serial0 (CH340), 115200. Never use the native USB port: 19/20 are the bus.
// Board settings / libraries = xelulu_testbench's (Adafruit GFX + Adafruit ST7735 and ST7789).
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#define PIN_TFT_SCLK 12
#define PIN_TFT_MOSI 11
#define PIN_TFT_CS   10
#define PIN_TFT_DC   14
#define PIN_TFT_RST  21
#define PIN_TFT_EN   45    // backlight, HIGH = on
#define PIN_SERVO_TX 20
#define PIN_SERVO_RX 19
#define SWING_DEG    90    // each way from 0; lower it if the joint hits the body
#define MOVE_MS      800   // time for each move, eased in and out
#define HOLD_MS      700   // pause at each stop
#define DIR          1     // -1 if "front" turns out to be the other way on your joint

static Adafruit_ST7789 tft(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
static HardwareSerial& bus = Serial1;
static bool echo;
static int servoId = -1;

// ---- SCS protocol, copied from xelulu_testbench ----
static uint8_t scsSum(const uint8_t* p, uint8_t n) {
  uint8_t s = 0;
  while (n--) s += *p++;
  return ~s;
}
// One request. -1 = nobody answered, 0 = garbled, 1 = valid reply
static int scs(uint8_t id, uint8_t inst, const uint8_t* par, uint8_t np, uint8_t* resp, uint8_t respLen) {
  uint8_t pkt[24] = {0xFF, 0xFF, id, (uint8_t)(np + 2), inst};
  uint8_t n = np + 6;
  if (np) memcpy(pkt + 5, par, np);
  pkt[n - 1] = scsSum(pkt + 2, np + 3);
  while (bus.available()) bus.read();
  bus.write(pkt, n);
  bus.flush();
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
static int scsPos(uint8_t id) {
  uint8_t b[2];
  return scsRead(id, 56, b, 2) == 1 ? (b[0] << 8 | b[1]) : -1;
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
static int findServo() {   // read the ID register (EEPROM addr 5) of whoever answers
  uint8_t b;
  for (int id = 0; id <= 253; id++)
    if (scsRead(id, 5, &b, 1) == 1) return b;
  return -1;
}

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
// Fixed-width text over a black background: redraws without flicker.
static void text(int x, int y, uint8_t size, uint16_t color, const char* fmt, ...) {
  char b[32];
  va_list a;
  va_start(a, fmt);
  vsnprintf(b, sizeof b, fmt, a);
  va_end(a);
  tft.setTextSize(size);
  tft.setTextColor(color, ST77XX_BLACK);
  tft.setCursor(x, y);
  tft.print(b);
}

// Ease from `from` to `to` counts. False = the servo stopped answering (unplugged / swapped).
static bool moveTo(int from, int to, const char* label) {
  text(4, 90, 4, ST77XX_WHITE, "%-6s", label);
  Serial0.printf("[spin] ID %d -> %s\n", servoId, label);
  const int steps = MOVE_MS / 20;
  for (int i = 1; i <= steps; i++) {
    scsGoal(servoId, from + (int)((to - from) * (1 - cosf(PI * i / steps)) / 2));
    delay(20);
  }
  for (uint32_t t0 = millis(); millis() - t0 < HOLD_MS; delay(50)) {
    int pos = scsPos(servoId);
    if (pos < 0 && (pos = scsPos(servoId)) < 0) return false;
    text(4, 150, 2, ST77XX_WHITE, "at %4d counts %+6.1f deg", pos, (pos - 512) * 300.0f / 1024);
  }
  return true;
}

void setup() {
  Serial0.begin(115200);
  tftBegin();
  text(4, 4, 2, ST77XX_CYAN, "TEST 7  SERVO SPIN +-%d", SWING_DEG);
  bus.setRxBufferSize(256);
  bus.begin(1000000, SERIAL_8N1, PIN_SERVO_RX, PIN_SERVO_TX);
  echoCheck();
  if (!echo) {
    text(4, 200, 2, ST77XX_RED, "NO ECHO: 20-1k-SIG-19");
    Serial0.println("[spin] echo MISSING: check 20 -> 1k -> SIGNAL <- 19");
  }
}

void loop() {
  if (servoId < 0) {
    tft.fillRect(0, 30, 320, 160, ST77XX_BLACK);
    text(4, 40, 2, ST77XX_YELLOW, "Plug in a servo...");
    if ((servoId = findServo()) < 0) return;
    tft.fillRect(0, 30, 320, 160, ST77XX_BLACK);
    text(4, 40, 3, ST77XX_GREEN, "ID %d", servoId);
    Serial0.printf("[spin] found servo ID %d\n", servoId);
    int pos = scsPos(servoId);
    if (pos < 0) { servoId = -1; return; }
    scsGoal(servoId, pos);   // torque on where it IS, then ease to 0: no jump
    scsTorque(servoId, true);
    if (!moveTo(pos, 512, "0")) { servoId = -1; return; }
  }
  const int a = DIR * SWING_DEG * 1024 / 300;   // SC09: 1024 counts over 300 deg
  if (!moveTo(512, 512 + a, "FRONT") || !moveTo(512 + a, 512, "0") ||
      !moveTo(512, 512 - a, "BACK") || !moveTo(512 - a, 512, "0")) {
    Serial0.printf("[spin] servo ID %d lost, rescanning\n", servoId);
    servoId = -1;
  }
}
