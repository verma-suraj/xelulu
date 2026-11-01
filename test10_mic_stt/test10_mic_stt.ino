// ============================================================
// TEST 10 - talk to the robot, see your words. The mics stream to the PC over the USB cable; the PC turns
// each sentence into text (Whisper, offline) and sends it back to this screen.
//   1. Flash this, keep the USB cable in.
//   2. On the PC:  python test10_mic_stt/stt_pc.py --port COM10     (--lang hi for Hindi, --model small = better)
//   3. Talk. After each sentence (a 0.6 s pause ends it) the words appear here and in the PC window.
// Robot side: both INMP441 (I2S as xelulu_fw: I2S0, 16 kHz, Philips, 32-bit stereo), L+R averaged (as
// xelulu_fw), 150 Hz high-pass (the fan's rumble), 16-bit, sent as base64 lines "A:<20 ms>" on Serial.
// From the PC: "T:<text>" (shown big, ASCII only: Hindi letters show as ?) and "S:<status>" (grey line).
// The green bar is the mic level: it must jump when you talk.
//
// FQBN esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,CDCOnBoot=cdc,USBMode=hwcdc,FlashMode=qio
// Libraries: Adafruit GFX + Adafruit ST7735 and ST7789 (display as test4 / test9).
// ============================================================
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "driver/i2s_std.h"
#include "mbedtls/base64.h"

// ---- pins: Xelulu_Wiring.md §3, §4 ----
#define PIN_TFT_SCLK   12
#define PIN_TFT_MOSI   11
#define PIN_TFT_CS     10
#define PIN_TFT_DC     14
#define PIN_TFT_RST    21
#define PIN_TFT_EN     45    // backlight, HIGH = on
#define PIN_MIC_BCLK    4    // both INMP441: SCK
#define PIN_MIC_WS      5    // WS
#define PIN_MIC_DIN     6    // SD
static const uint8_t MOTOR_PINS[] = {1, 2, 42, 41, 13, 47};   // TB6612 STBY is tied high: keep these LOW

// ---- knobs ----
#define RATE     16000
#define FRAME    320     // 20 ms per line to the PC
#define HPF_HZ   150     // 2nd-order high-pass: the fan's rumble out, voices stay
#define GAIN_DB  0       // on top of 18 -> 16 bits. The fan reads ~-35 dBFS and close speech ~-10: no room for more

#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_CYAN   0x07FF
#define C_GREEN  0x07E0
#define C_GREY   0x8410

// ============================================================ display: ST7789 320x240 landscape
static Adafruit_ST7789 tft(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
static GFXcanvas16 strip(320, 40);   // a row is drawn here and pushed in one go: no flicker

static void row(int y, uint16_t color, const char* text) {
  strip.fillScreen(C_BLACK);
  strip.setTextSize(2);
  strip.setTextColor(color);
  strip.setCursor(4, 12);
  strip.print(text);
  tft.drawRGBBitmap(0, y, strip.getBuffer(), 320, 40);
}
static void tftBegin() {   // Display_test_code's sequence, step for step (as test4)
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
  tft.setRotation(1);   // upright as mounted
  tft.fillScreen(C_BLACK);
}
static void levelBar(float db) {   // -70..-10 dBFS
  strip.fillScreen(C_BLACK);
  strip.drawRect(0, 6, 320, 28, C_GREY);
  strip.fillRect(2, 8, constrain((int)((db + 70) * 316 / 60), 0, 316), 24, C_GREEN);
  tft.drawRGBBitmap(0, 40, strip.getBuffer(), 320, 40);
}
static void showText(const char* t) {   // word-wrapped, size 2: 26 characters a line, 5 lines
  tft.fillRect(0, 120, 320, 120, C_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(C_WHITE);
  char ln[27];
  int n = 0, y = 122;
  while (*t && y < 222) {
    int w = min((int)strcspn(t, " "), 26);
    if (n && n + 1 + w > 26) {   // the word doesn't fit: next line
      ln[n] = 0;
      tft.setCursor(4, y);
      tft.print(ln);
      y += 20;
      n = 0;
      continue;
    }
    if (n) ln[n++] = ' ';
    memcpy(ln + n, t, w);
    n += w;
    t += w;
    while (*t == ' ') t++;
  }
  if (n && y < 222) {
    ln[n] = 0;
    tft.setCursor(4, y);
    tft.print(ln);
  }
}

// ============================================================ mics: I2S0 RX, set up as xelulu_fw Audio.cpp initRx
static i2s_chan_handle_t rx;
static bool micBegin() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.dma_desc_num = 8;   // 80 ms: room for a screen redraw between reads
  cc.dma_frame_num = 160;
  i2s_std_config_t sc = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {I2S_GPIO_UNUSED, (gpio_num_t)PIN_MIC_BCLK, (gpio_num_t)PIN_MIC_WS,
                 I2S_GPIO_UNUSED, (gpio_num_t)PIN_MIC_DIN, {false, false, false}},
  };
  return i2s_new_channel(&cc, nullptr, &rx) == ESP_OK && i2s_channel_init_std_mode(rx, &sc) == ESP_OK &&
         i2s_channel_enable(rx) == ESP_OK;
}

static float hb0, hb1, hb2, ha1, ha2, hz1, hz2;   // the HPF_HZ high-pass (RBJ cookbook, Q 0.707) + its state

void setup() {
  for (uint8_t p : MOTOR_PINS) {   // FIRST: wheels still
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
  Serial.setTxBufferSize(16384);   // ~0.4 s of audio lines: rides out a busy PC
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);        // never block: with no PC reading, frames are dropped
  tftBegin();
  row(0, C_CYAN, "TEST 10  TALK TO ME");
  row(80, C_GREY, "PC: run stt_pc.py");
  float w = 2 * PI * HPF_HZ / RATE, cw = cosf(w), al = sinf(w) / (2 * 0.7071f), a0 = 1 + al;
  hb0 = hb2 = (1 + cw) / 2 / a0;
  hb1 = -(1 + cw) / a0;
  ha1 = -2 * cw / a0;
  ha2 = (1 - al) / a0;
  bool up = micBegin();
  Serial.printf("[test10] mic I2S %s, streaming 16 kHz 16-bit mono as A: lines\n", up ? "ok" : "FAILED");
  if (!up) {
    row(80, C_WHITE, "mic I2S FAILED");
    for (;;) delay(1000);
  }
}

void loop() {
  static int32_t raw[FRAME * 2];   // L, R, L, R...
  static int16_t pcm[FRAME];
  static uint8_t b64[900];         // 640 bytes -> 856 characters
  static char in[256];             // a line from the PC
  static int inN;
  static uint8_t n;
  static float lv;
  const float g = powf(10.0f, GAIN_DB / 20.0f) / 4;   // 18-bit -> 16-bit
  size_t got;
  i2s_channel_read(rx, raw, sizeof raw, &got, portMAX_DELAY);
  double e = 0;
  for (int i = 0; i < FRAME; i++) {
    float x = ((raw[2 * i] >> 14) + (raw[2 * i + 1] >> 14)) * 0.5f;   // L+R average, 18-bit
    float y = hb0 * x + hz1;                                           // high-pass, transposed direct form II
    hz1 = hb1 * x - ha1 * y + hz2;
    hz2 = hb2 * x - ha2 * y;
    float s = constrain(y * g, -32768.0f, 32767.0f);
    pcm[i] = (int16_t)s;
    e += s * s;
  }
  size_t len;
  mbedtls_base64_encode(b64, sizeof b64, &len, (const uint8_t*)pcm, sizeof pcm);
  if (Serial.availableForWrite() >= (int)len + 4) {   // whole lines only: a torn one would garble the PC's decode
    Serial.print("A:");
    Serial.write(b64, len);
    Serial.print('\n');
  }
  lv += (10 * log10f(e / FRAME / (32768.0 * 32768.0) + 1e-12) - lv) * 0.3f;
  if (++n % 5 == 0) levelBar(lv);   // 10 Hz
  while (Serial.available()) {      // the PC's answers
    char c = Serial.read();
    if (c == '\n') {
      in[inN] = 0;
      if (!strncmp(in, "T:", 2)) showText(in + 2);
      else if (!strncmp(in, "S:", 2)) row(80, C_GREY, in + 2);
      inN = 0;
    } else if (c != '\r' && inN < (int)sizeof in - 1) in[inN++] = c;
  }
}
