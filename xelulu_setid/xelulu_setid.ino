// Xelulu servo ID setter: plug in ONE SC09, it finds the servo's current ID and saves NEW_ID
// into the servo's EEPROM. Everything shows on the TFT, no USB cable needed.
// To prove the ID was saved: switch the battery OFF and ON again. The screen must show
// "already ID 1" (the ID survived power-off).
// Wiring per Xelulu_Wiring.md: GPIO20 -> 1 kOhm -> SIGNAL <- GPIO19. Battery ON (servo + display).
// Console = Serial0 (CH340), 115200, same messages as the screen.
// Board settings / libraries = xelulu_testbench's (Adafruit GFX + Adafruit ST7735 and ST7789).
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#define NEW_ID       1     // the ID to give the connected servo
#define PIN_TFT_SCLK 12
#define PIN_TFT_MOSI 11
#define PIN_TFT_CS   10
#define PIN_TFT_DC   14
#define PIN_TFT_RST  21
#define PIN_TFT_EN   45    // backlight, HIGH = on
#define PIN_SERVO_TX 20
#define PIN_SERVO_RX 19

static Adafruit_ST7789 tft(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
static HardwareSerial& bus = Serial1;
static bool echo;

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
// One line of text on the screen and the console.
static void say(int y, uint8_t size, uint16_t color, const char* fmt, ...) {
  char b[40];
  va_list a;
  va_start(a, fmt);
  vsnprintf(b, sizeof b, fmt, a);
  va_end(a);
  tft.fillRect(0, y, 320, 8 * size + 4, ST77XX_BLACK);
  tft.setTextSize(size);
  tft.setTextColor(color);
  tft.setCursor(4, y);
  tft.print(b);
  Serial0.printf("[setid] %s\n", b);
}

void setup() {
  Serial0.begin(115200);
  tftBegin();
  say(4, 2, ST77XX_CYAN, "SERVO ID SETTER -> ID %d", NEW_ID);
  bus.setRxBufferSize(256);
  bus.begin(1000000, SERIAL_8N1, PIN_SERVO_RX, PIN_SERVO_TX);
  echoCheck();
  if (!echo) say(30, 2, ST77XX_RED, "NO ECHO: check 20-1k-SIG-19");

  int id;
  say(60, 2, ST77XX_YELLOW, "Searching for servo...");
  while ((id = findServo()) < 0) delay(500);
  say(60, 3, ST77XX_WHITE, "Found ID %d", id);

  if (id == NEW_ID) say(110, 3, ST77XX_GREEN, "already ID %d", NEW_ID);
  else if (scsSetId(id, NEW_ID)) {
    say(110, 3, ST77XX_GREEN, "ID %d -> %d OK", id, NEW_ID);
    say(160, 2, ST77XX_YELLOW, "Battery OFF then ON:");
    say(184, 2, ST77XX_YELLOW, "must show 'already ID %d'", NEW_ID);
  } else say(110, 3, ST77XX_RED, "SET ID FAILED");
}

void loop() {}
