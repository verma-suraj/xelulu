// TEST 2 - N20 wheels alone: after a 5 s countdown, the LEFT N20 runs forward then backward, then
// the RIGHT one. Each run shows its encoder counts. Then the encoders stay live: turn a wheel
// by hand and watch the count. Press BOOT (GPIO0) to run the wheels again. LIFT THE ROBOT.
// Wiring per Xelulu_Wiring.md: TB6612 A = left (PWMA 1, AIN1 2, AIN2 42), B = right (PWMB 41,
// BIN1 13, BIN2 47), STBY tied high. Encoders C1/C2: left 17/48, right 18/3. Battery ON.
// Doesn't touch the servo pins 19/20, so the native USB (COM7) stays up: log on `Serial`, 115200.
// Board settings / libraries = xelulu_testbench's (Adafruit GFX + Adafruit ST7735 and ST7789).
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "driver/pulse_cnt.h"

#define PIN_TFT_SCLK 12
#define PIN_TFT_MOSI 11
#define PIN_TFT_CS   10
#define PIN_TFT_DC   14
#define PIN_TFT_RST  21
#define PIN_TFT_EN   45    // backlight, HIGH = on
#define PIN_BOOT      0    // BOOT button, LOW = pressed
static const uint8_t MOT_PWM[2] = {1, 41}, MOT_IN1[2] = {2, 13}, MOT_IN2[2] = {42, 47};   // left, right
static const uint8_t ENC_C1[2] = {17, 18}, ENC_C2[2] = {48, 3};
#define WHEEL_DUTY   0.6f  // 0..1
#define RUN_MS       1500  // each run at full duty

static Adafruit_ST7789 tft(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
static pcnt_unit_handle_t enc[2];
static const char* const NAME[2] = {"LEFT", "RIGHT"};

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

// ---- wheels: copied from xelulu_testbench ----
static void wheelsBegin() {
  for (uint8_t m = 0; m < 2; m++) {
    ledcAttach(MOT_PWM[m], 20000, 10);    // 20 kHz, as xelulu_fw
    pcnt_unit_config_t uc = {};
    uc.low_limit = -30000;
    uc.high_limit = 30000;
    uc.flags.accum_count = 1;
    pcnt_new_unit(&uc, &enc[m]);
    pcnt_glitch_filter_config_t gf = {.max_glitch_ns = 1000};
    pcnt_unit_set_glitch_filter(enc[m], &gf);
    // x4 quadrature: each channel's edges, signed by the other's level
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
static int count(uint8_t m) {
  int c = 0;
  pcnt_unit_get_count(enc[m], &c);
  return c;
}
static void motor(uint8_t m, float d) {   // d -1..1, + = IN1 high
  digitalWrite(MOT_IN1[m], d > 0);
  digitalWrite(MOT_IN2[m], d < 0);
  ledcWrite(MOT_PWM[m], (uint32_t)(fabsf(d) * 1023));
}
static void motorStop(uint8_t m) {   // TB6612 short brake, then all pins low
  digitalWrite(MOT_IN1[m], HIGH);
  digitalWrite(MOT_IN2[m], HIGH);
  ledcWrite(MOT_PWM[m], 1023);
  delay(100);
  motor(m, 0);
}

static void runWheels() {
  tft.fillRect(0, 30, 320, 210, ST77XX_BLACK);
  for (int s = 5; s > 0; s--) {
    text(4, 40, 2, ST77XX_YELLOW, "Spin in %d s: LIFT ROBOT", s);
    delay(1000);
  }
  for (uint8_t m = 0; m < 2; m++)
    for (uint8_t d = 0; d < 2; d++) {
      const char* dir = d ? "backward" : "forward";
      text(4, 40, 2, ST77XX_YELLOW, "%s N20 %s", NAME[m], dir);
      int before[2] = {count(0), count(1)};
      for (int i = 1; i <= 30; i++) {   // 0.3 s soft start
        motor(m, (d ? -WHEEL_DUTY : WHEEL_DUTY) * i / 30);
        delay(10);
      }
      delay(RUN_MS);
      motorStop(m);
      int own = count(m) - before[m], other = count(1 - m) - before[1 - m];
      // a good run: its own encoder moved, the sign follows the direction, the other wheel stayed put
      text(4, 70 + (2 * m + d) * 24, 2, own ? ST77XX_GREEN : ST77XX_RED, "%c %-4s %+6d %s", NAME[m][0],
           d ? "back" : "fwd", own, own ? "" : "NO ENC");
      Serial.printf("[n20] %s %s: own encoder %+d, other %+d%s\n", NAME[m], dir, own, other,
                    own ? "" : "   <- no counts: did the wheel turn? encoder wires / 3.3 V?");
      delay(800);
    }
  text(4, 40, 2, ST77XX_CYAN, "Done. BOOT = again");
  text(4, 172, 1, ST77XX_WHITE, "fwd and back should have opposite signs");
}

void setup() {
  for (uint8_t m = 0; m < 2; m++)   // FIRST: STBY is tied high, these pins LOW keep the wheels still
    for (uint8_t p : {MOT_PWM[m], MOT_IN1[m], MOT_IN2[m]}) {
      pinMode(p, OUTPUT);
      digitalWrite(p, LOW);
    }
  pinMode(PIN_BOOT, INPUT_PULLUP);
  Serial.begin(115200);
  tftBegin();
  text(4, 4, 2, ST77XX_CYAN, "TEST 2  N20 WHEELS");
  wheelsBegin();
  runWheels();
}

void loop() {
  text(4, 200, 2, ST77XX_WHITE, "Enc L %+7d R %+7d", count(0), count(1));
  static uint32_t printAt;
  if (millis() - printAt >= 500) {
    printAt = millis();
    Serial.printf("enc L %+d  R %+d\n", count(0), count(1));
  }
  if (digitalRead(PIN_BOOT) == LOW) runWheels();
  delay(50);
}
