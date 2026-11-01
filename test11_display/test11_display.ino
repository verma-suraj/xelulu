// ============================================================
// TEST 11 - the display: fills the WHOLE screen with one colour at a time, round and round:
//   RED -> GREEN -> BLUE -> WHITE -> BLACK, HOLD_MS each.
// Look for: dots that stay dark or stay lit, lines or bands, dim or blotchy patches, an edge strip
// that never changes, a wrong colour (RED showing blue = colour order swapped).
// Battery ON: the display runs off the 3.3 V rail, which USB alone doesn't power.
// Console = native USB (COM10), 115200: the colour now on screen.
// Doesn't touch the servo pins 19/20, so the native USB stays up for the next flash.
//
// FQBN esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,CDCOnBoot=cdc,USBMode=hwcdc,FlashMode=qio
// Libraries: Adafruit GFX + Adafruit ST7735 and ST7789 (display set up as Display_test_code, as test4/test9).
// ============================================================
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ---- pins: Xelulu_Wiring.md §3 ----
#define PIN_TFT_SCLK   12
#define PIN_TFT_MOSI   11
#define PIN_TFT_CS     10
#define PIN_TFT_DC     14
#define PIN_TFT_RST    21
#define PIN_TFT_EN     45    // backlight, HIGH = on
static const uint8_t MOTOR_PINS[] = {1, 2, 42, 41, 13, 47};   // TB6612 STBY is tied high: keep these LOW

#define HOLD_MS  4000   // each colour this long

static const struct { uint16_t color; const char* name; } COLORS[] = {
  {ST77XX_RED, "RED"}, {ST77XX_GREEN, "GREEN"}, {ST77XX_BLUE, "BLUE"}, {ST77XX_WHITE, "WHITE"}, {ST77XX_BLACK, "BLACK"},
};

static Adafruit_ST7789 tft(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

void setup() {
  for (uint8_t p : MOTOR_PINS) {   // FIRST: wheels still
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);   // never block on a console nobody is reading
  delay(500);                 // Display_test_code's sequence, step for step (as test4/test9)
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
  tft.init(240, 320);   // ponytail: no setRotation, a full-screen fill looks the same either way
}

void loop() {
  for (auto& c : COLORS) {
    tft.fillScreen(c.color);
    Serial.printf("[test11] %s\n", c.name);
    delay(HOLD_MS);
  }
}
