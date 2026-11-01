// TEST 5 - ToF: 2x VL53L0X V2 behind the TCA9548A mux, CH0 = front-left, CH1 = front-right.
// Live distance in mm for both on the TFT, plus min / max seen. Hold a hand or a card in front
// of each one and move it: the number must follow. "far" = nothing within ~2 m.
// If a sensor shows MISSING, the console says what answered: the mux at 0x70 and a 0x29 on its channel.
// Press BOOT (GPIO0) to clear min / max.
// Wiring per Xelulu_Wiring.md section 8: mux on I2C SDA 8 / SCL 9. Battery ON.
// Console = Serial (native USB COM7), 115200.
// Board settings / libraries = xelulu_testbench's (Adafruit GFX + Adafruit ST7735 and ST7789 + VL53L0X Pololu).
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <VL53L0X.h>

#define PIN_I2C_SDA   8
#define PIN_I2C_SCL   9
#define PIN_TFT_SCLK 12
#define PIN_TFT_MOSI 11
#define PIN_TFT_CS   10
#define PIN_TFT_DC   14
#define PIN_TFT_RST  21
#define PIN_TFT_EN   45    // backlight, HIGH = on
#define PIN_BOOT      0    // BOOT button, LOW = pressed
#define MUX        0x70
static const uint8_t MOTOR_PINS[] = {1, 2, 42, 41, 13, 47};   // TB6612 STBY is tied high: keep these LOW
static const char* const NAME[2] = {"FRONT-LEFT ", "FRONT-RIGHT"};

static Adafruit_ST7789 tft(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
static VL53L0X tof[2];
static bool up[2];
static uint16_t mm[2], lo[2], hi[2];

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
  char b[32];
  va_list a;
  va_start(a, fmt);
  vsnprintf(b, sizeof b, fmt, a);
  va_end(a);
  tft.setTextSize(size);
  tft.setTextColor(color, ST77XX_BLACK);
  tft.setCursor(x, y);
  tft.printf("%-*s", (320 - x) / (6 * size), b);
}

// ---- I2C ----
static bool i2cRead(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(addr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}
static bool muxSelect(uint8_t ch) {
  Wire.beginTransmission(MUX);
  Wire.write(1 << ch);
  return Wire.endTransmission() == 0;
}
static void clearMinMax() {
  for (uint8_t c = 0; c < 2; c++) lo[c] = 9999, hi[c] = 0;
}

void setup() {
  for (uint8_t p : MOTOR_PINS) {   // FIRST: wheels still
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
  pinMode(PIN_BOOT, INPUT_PULLUP);
  Serial.begin(115200);
  tftBegin();
  text(4, 4, 2, ST77XX_CYAN, "TEST 5  ToF VL53L0X");
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  Wire.setTimeOut(25);
  bool mux = muxSelect(0);
  Serial.printf("[tof] mux 0x70 %s\n", mux ? "ok" : "NOT answering: SDA 8 / SCL 9, VIN 3.3 V, A0-A2 GND, RST 3.3 V");
  if (!mux) text(4, 200, 2, ST77XX_RED, "MUX 0x70 not found");
  for (uint8_t c = 0; c < 2; c++) {
    muxSelect(c);
    uint8_t id = 0;
    bool seen = i2cRead(0x29, 0xC0, &id, 1);
    tof[c].setBus(&Wire);
    tof[c].setTimeout(100);
    up[c] = seen && id == 0xEE && tof[c].init();
    if (up[c]) tof[c].startContinuous();
    Serial.printf("[tof] CH%u %s: %s\n", c, NAME[c], up[c] ? "ok" : !seen ? "nothing at 0x29 (SDA/SCL on this channel? VIN? XSHUT low?)"
                                                              : "0x29 answers but is not a VL53L0X");
  }
  clearMinMax();
}

void loop() {
  for (uint8_t c = 0; c < 2; c++) {
    int y = 40 + c * 80;
    text(4, y, 2, ST77XX_WHITE, "CH%u %s", c, NAME[c]);
    if (!up[c]) {
      text(4, y + 22, 4, ST77XX_RED, "MISSING");
      continue;
    }
    muxSelect(c);
    uint16_t r = tof[c].readRangeContinuousMillimeters();
    if (tof[c].timeoutOccurred()) {
      text(4, y + 22, 4, ST77XX_RED, "TIMEOUT");
      continue;
    }
    mm[c] = r;
    bool far = r >= 2000;   // 8190 / 8191 = no target
    if (!far) lo[c] = min(lo[c], r), hi[c] = max(hi[c], r);
    if (far) text(4, y + 22, 4, ST77XX_YELLOW, "far");
    else text(4, y + 22, 4, ST77XX_GREEN, "%4u mm", r);
    text(4, y + 58, 1, ST77XX_WHITE, "min %u  max %u", lo[c] == 9999 ? 0 : lo[c], hi[c]);
  }
  static uint32_t printAt;
  if (millis() - printAt >= 250) {
    printAt = millis();
    Serial.printf("ToF L %s%u  R %s%u mm\n", up[0] ? "" : "MISSING ", mm[0], up[1] ? "" : "MISSING ", mm[1]);
  }
  if (digitalRead(PIN_BOOT) == LOW) clearMinMax();
  text(4, 216, 1, ST77XX_CYAN, "BOOT = clear min/max");
}
