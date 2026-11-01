// TEST 6 - gyro (GY-87's MPU6050 at 0x68): gyro deg/s, accel g, tilt, chip temperature on the TFT.
// Keep the robot STILL for 2 s at boot: it learns the gyro's zero offset. Then:
//   - tilt it forward / sideways: pitch / roll follow, acc z is about +1.00 g lying flat
//   - turn it on the table by 90 deg: TURN shows about 90 (gyro z added up over time)
//   - still, the gyro numbers should sit near 0 (a few deg/s of jitter is fine)
// Press BOOT (GPIO0) to learn the zero again and reset TURN (keep it still).
// Wiring per Xelulu_Wiring.md: GY-87 on I2C SDA 8 / SCL 9. Battery ON.
// Console = Serial (native USB COM7), 115200.
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
#define MPU        0x68
static const uint8_t MOTOR_PINS[] = {1, 2, 42, 41, 13, 47};   // TB6612 STBY is tied high: keep these LOW

static Adafruit_ST7789 tft(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
static bool up;
static float gyro[3], acc[3], bias[3], temp, turn;

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

// ---- MPU6050: xelulu_testbench's setup ----
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
static bool readImu() {
  uint8_t b[14];
  if (!i2cRead(MPU, 0x3B, b, 14)) return false;
  for (int i = 0; i < 3; i++) {
    acc[i] = (int16_t)(b[2 * i] << 8 | b[2 * i + 1]) / 8192.0f;              // +-4 g
    gyro[i] = (int16_t)(b[8 + 2 * i] << 8 | b[9 + 2 * i]) / 32.8f - bias[i];   // +-1000 deg/s
  }
  temp = (int16_t)(b[6] << 8 | b[7]) / 340.0f + 36.53f;
  return true;
}
static void calibrate() {   // average 2 s of a still gyro = its zero offset
  text(4, 30, 2, ST77XX_YELLOW, "Keep STILL: zeroing...");
  float sum[3] = {};
  memset(bias, 0, sizeof bias);
  int n = 0;
  for (uint32_t t0 = millis(); millis() - t0 < 2000; delay(5))
    if (readImu()) {
      for (int i = 0; i < 3; i++) sum[i] += gyro[i];
      n++;
    }
  for (int i = 0; i < 3; i++) bias[i] = n ? sum[i] / n : 0;
  turn = 0;
  text(4, 30, 2, ST77XX_GREEN, "Zeroed. BOOT = again");
  Serial.printf("[gyro] zero offset x %+.2f y %+.2f z %+.2f deg/s (%d samples)\n", bias[0], bias[1], bias[2], n);
}

void setup() {
  for (uint8_t p : MOTOR_PINS) {   // FIRST: wheels still
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
  pinMode(PIN_BOOT, INPUT_PULLUP);
  Serial.begin(115200);
  tftBegin();
  text(4, 4, 2, ST77XX_CYAN, "TEST 6  GYRO MPU6050");
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  Wire.setTimeOut(25);
  uint8_t who = 0;
  up = i2cRead(MPU, 0x75, &who, 1);
  if (!up) {
    text(4, 60, 2, ST77XX_RED, "MPU6050 not found (0x68)");
    Serial.println("[gyro] nothing at 0x68: SDA 8 / SCL 9, GY-87 VCC + GND, battery ON?");
    return;
  }
  Serial.printf("[gyro] WHO_AM_I 0x%02X %s\n", who, who == 0x68 ? "(MPU6050)" : "(not 0x68: a clone chip, readings may still be fine)");
  i2cWrite(MPU, 0x6B, 0x01);   // wake; then xelulu_fw's ranges: DLPF 44 Hz, +-1000 deg/s, +-4 g
  i2cWrite(MPU, 0x1A, 0x03);
  i2cWrite(MPU, 0x1B, 0x10);
  i2cWrite(MPU, 0x1C, 0x08);
  delay(100);
  calibrate();
}

void loop() {
  if (!up) return;
  static uint32_t lastUs = micros();
  if (!readImu()) {
    text(4, 60, 2, ST77XX_RED, "read FAILED");
    delay(100);
    return;
  }
  uint32_t now = micros();
  turn += gyro[2] * (now - lastUs) / 1e6f;
  lastUs = now;
  float pitch = atan2f(-acc[0], sqrtf(acc[1] * acc[1] + acc[2] * acc[2])) * 180 / PI;
  float roll = atan2f(acc[1], acc[2]) * 180 / PI;

  static uint32_t drawAt, printAt;
  if (millis() - drawAt >= 100) {
    drawAt = millis();
    text(4, 60, 2, ST77XX_WHITE, "Gyro x%+5.0f y%+5.0f z%+5.0f", gyro[0], gyro[1], gyro[2]);
    text(4, 86, 2, ST77XX_WHITE, "Acc  x%+5.2f y%+5.2f z%+5.2f", acc[0], acc[1], acc[2]);
    text(4, 120, 3, ST77XX_GREEN, "Pitch %+6.1f", pitch);
    text(4, 150, 3, ST77XX_GREEN, "Roll  %+6.1f", roll);
    text(4, 180, 3, ST77XX_ORANGE, "TURN  %+6.1f", turn);
    text(4, 216, 2, ST77XX_WHITE, "chip %.1f C", temp);
  }
  if (millis() - printAt >= 250) {
    printAt = millis();
    Serial.printf("gyro %+.1f %+.1f %+.1f  acc %+.2f %+.2f %+.2f  pitch %+.1f roll %+.1f turn %+.1f  %.1fC\n", gyro[0],
                  gyro[1], gyro[2], acc[0], acc[1], acc[2], pitch, roll, turn, temp);
  }
  if (digitalRead(PIN_BOOT) == LOW) {
    calibrate();
    lastUs = micros();
  }
  delay(5);
}
