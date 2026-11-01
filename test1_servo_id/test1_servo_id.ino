// TEST 1 - servo ID (+ angle reader): plug in ONE SC09, turn it by hand, read its ID and angle on the TFT.
// Shows the ID saved in the servo, the live position (counts + degrees from 512 = mid-travel),
// and the MIN / MAX positions seen since boot: sweep the joint to both end stops, write them down.
// Press BOOT (GPIO0) to clear MIN / MAX. Unplug and plug in another servo any time: it rescans.
// Nothing is saved to the servo. The only write is torque OFF (a RAM setting, gone at power-off)
// so the joint turns freely by hand.
// Wiring per Xelulu_Wiring.md: GPIO20 -> 1 kOhm -> SIGNAL <- GPIO19. Battery ON (servo + display).
// Console = Serial0 (CH340), 115200, same numbers as the screen.
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
#define PIN_BOOT      0    // BOOT button, LOW = pressed
#define DEG_PER_COUNT (300.0f / 1024)   // SC09: 1024 counts over 300 deg

static Adafruit_ST7789 tft(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
static HardwareSerial& bus = Serial1;
static bool echo;
static int servoId = -1, lo, hi;

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
static int scsPos(uint8_t id) {
  uint8_t b[2];
  return scsRead(id, 56, b, 2) == 1 ? (b[0] << 8 | b[1]) : -1;
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
static float deg(int pos) { return (pos - 512) * DEG_PER_COUNT; }

void setup() {
  Serial0.begin(115200);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  tftBegin();
  text(4, 4, 2, ST77XX_CYAN, "TEST 1  SERVO ID");
  bus.setRxBufferSize(256);
  bus.begin(1000000, SERIAL_8N1, PIN_SERVO_RX, PIN_SERVO_TX);
  echoCheck();
  if (!echo) {
    text(4, 40, 2, ST77XX_RED, "NO ECHO: check wiring");
    text(4, 64, 2, ST77XX_RED, "20 -> 1k -> SIG <- 19");
    Serial0.println("[angle] echo MISSING: check 20 -> 1k -> SIGNAL <- 19");
  }
}

void loop() {
  if (servoId < 0) {
    text(4, 40, 2, ST77XX_YELLOW, "Searching for servo...  ");
    servoId = findServo();
    if (servoId < 0) return;
    uint8_t p[2] = {40, 0}, r[6];
    scs(servoId, 0x03, p, 2, r, 6);   // torque off (RAM only): turn it by hand
    lo = 1023, hi = 0;
    tft.fillRect(0, 30, 320, 210, ST77XX_BLACK);
    Serial0.printf("[angle] found servo ID %d\n", servoId);
  }
  int pos = scsPos(servoId);
  if (pos < 0 && (pos = scsPos(servoId)) < 0) {   // one retry, then rescan (unplugged / swapped)
    Serial0.printf("[angle] servo ID %d lost, rescanning\n", servoId);
    tft.fillRect(0, 30, 320, 210, ST77XX_BLACK);
    servoId = -1;
    return;
  }
  if (digitalRead(PIN_BOOT) == LOW) lo = 1023, hi = 0;
  lo = min(lo, pos), hi = max(hi, pos);

  text(4, 36, 3, ST77XX_GREEN, "ID %-3d", servoId);
  text(4, 80, 5, ST77XX_WHITE, "%+6.1f", deg(pos));
  text(190, 94, 2, ST77XX_WHITE, "deg");
  text(4, 132, 2, ST77XX_WHITE, "pos %4d counts", pos);
  text(4, 164, 2, ST77XX_ORANGE, "MIN %4d  %+6.1f deg", lo, deg(lo));
  text(4, 188, 2, ST77XX_ORANGE, "MAX %4d  %+6.1f deg", hi, deg(hi));
  text(4, 220, 1, ST77XX_CYAN, "BOOT = clear MIN/MAX   0 deg = 512 counts");

  static uint32_t last;
  if (millis() - last > 250) {
    last = millis();
    Serial0.printf("ID %d  pos %d (%+.1f deg)  min %d (%+.1f)  max %d (%+.1f)\n", servoId, pos, deg(pos), lo,
                   deg(lo), hi, deg(hi));
  }
  delay(30);
}
