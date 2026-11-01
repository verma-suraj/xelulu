// ============================================================
// TEST 9 - the two mics, LEFT and RIGHT channel. 2x SPH0645 share one data line (Xelulu_Wiring.md §4):
//   LEFT  = the mic with SEL on GND   -> the left slot
//   RIGHT = the mic with SEL on 3.3 V -> the right slot
// Live on the robot's screen: a level bar per channel (white tick = loudest moment of the last 1.5 s)
// and which side is louder. Scratch, tap or blow on ONE mic: its bar must jump and the bottom line
// must name its side. Swapped sides = xelulu_fw's sound direction (dir_deg) would come out mirrored.
//   DEAD                         = no mic drives that slot: its low bits never move (a floating line reads 0
//                                  with stray high bits). No power, DOUT loose, or both SELs on one rail
//   both bars high in silence    = garbled samples: try MSB_FORMAT 1 (the SPH0645 bit-shift quirk)
// I2S as xelulu_fw Audio.cpp: I2S0 master, Philips, 32-bit stereo, 18-bit samples (>> 14). The clock cycles
// 16 / 32 / 48 kHz, 4 s each (BCLK = 64 x rate = 1.0 / 2.0 / 3.1 MHz; xelulu_fw runs 16 kHz, the SPH0645's
// slowest clock): a mic that only wakes on a faster clock comes alive on the later rates.
// Software can't switch one mic off (both share BCLK, LRCL and DOUT): to hear the RIGHT mic alone,
// unplug the LEFT mic's DOUT wire. Then any live bar, in either slot, is the right mic.
// Console = native USB, 115200, 10+ lines/s: rate; per channel rms dBFS, peak dBFS, DC; then the louder side.
// Doesn't touch the servo pins 19/20, so the native USB stays up.
//
// FQBN esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,CDCOnBoot=cdc,USBMode=hwcdc,FlashMode=qio
// Libraries: Adafruit GFX + Adafruit ST7735 and ST7789 (display set up as Display_test_code, as test4).
// ============================================================
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "driver/i2s_std.h"

// ---- pins: Xelulu_Wiring.md §3, §4 ----
#define PIN_TFT_SCLK   12
#define PIN_TFT_MOSI   11
#define PIN_TFT_CS     10
#define PIN_TFT_DC     14
#define PIN_TFT_RST    21
#define PIN_TFT_EN     45    // backlight, HIGH = on
#define PIN_MIC_BCLK    4    // both SPH0645
#define PIN_MIC_WS      5
#define PIN_MIC_DIN     6
static const uint8_t MOTOR_PINS[] = {1, 2, 42, 41, 13, 47};   // TB6612 STBY is tied high: keep these LOW

// ---- knobs ----
static const uint32_t RATES[] = {16000, 32000, 48000};   // [0] = xelulu_fw's
#define RATE_MS      4000    // each rate this long, round and round
#define BLOCK        160     // 10 ms
#define WIN          5       // blocks per reading: 50 ms
#define MSB_FORMAT   0       // = xelulu_fw cal.micMsbFormat
#define ONLY         0       // 0 both slots; 1 = keep only the LEFT slot, 2 = only the RIGHT (I2S mono). Both mics
                             // still talk on the wire (they share LRCL): this picks which half of each frame is kept
#define CH           (ONLY ? 1 : 2)   // samples per frame in the buffer
#define PULL_UP      0       // 1 = the ESP's pull-up on DOUT: a slot no mic drives then reads all ones (dc -1),
                             // one held low still reads 0. 0 = as xelulu_fw (no pull)
#define RAW_LOG      1       // 1 = each log line also shows 4 frames' raw 32-bit words per slot (hex): where the bits land
#define HPF_HZ       150     // the log also gives each slot's level after a 2nd-order high-pass here ("hp"):
                             // how much of the room noise is rumble that a filter would take out
#define ACTIVE_DBFS  -50     // a reading counts as a sound above this (silence is about -85)...
#define SIDE_DB      3       // ...and names a side when that side is this much louder

#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_CYAN   0x07FF
#define C_GREEN  0x07E0
#define C_YELLOW 0xFFE0
#define C_RED    0xF800
#define C_DKRED  0x8000
#define C_GREY   0x4208

struct Chan { float rms, peak, dc, hp; bool dead; };   // one 50 ms reading; dBFS, full scale = 2^17 (18 bits)

// ============================================================ display: ST7789 320x240 landscape
static Adafruit_ST7789 tft(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
static GFXcanvas16 strip(320, 40);   // a row is drawn here and pushed in one go: no flicker

static void push(int y) { tft.drawRGBBitmap(0, y, strip.getBuffer(), 320, 40); }
static void row(int y, uint8_t size, uint16_t color, const char* text) {
  strip.fillScreen(C_BLACK);
  strip.setTextSize(size);
  strip.setTextColor(color);
  strip.setCursor(4, 20 - 4 * size);
  strip.print(text);
  push(y);
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

static int px(float db) { return constrain((int)((db + 90) * 208 / 90), 0, 208); }   // -90..0 dBFS
static void bar(int y, const char* name, float rms, float hold, bool dead) {
  strip.fillScreen(C_BLACK);
  strip.setTextSize(3);
  strip.setTextColor(C_WHITE);
  strip.setCursor(4, 8);
  strip.print(name);
  strip.drawRect(26, 4, 212, 32, C_GREY);
  strip.setTextSize(2);
  strip.setCursor(246, 12);
  if (dead) {
    strip.fillRect(28, 6, 208, 28, C_DKRED);
    strip.setTextColor(C_RED);
    strip.print("DEAD");
  } else {
    strip.fillRect(28, 6, px(rms), 28, C_GREEN);
    strip.fillRect(27 + px(hold), 6, 3, 28, C_WHITE);   // peak hold
    strip.printf("%4.0f", rms);
  }
  push(y);
}

// ============================================================ mics: I2S0 RX, set up as xelulu_fw Audio.cpp initRx
static i2s_chan_handle_t rx;
static bool micBegin() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.dma_desc_num = 8;   // 80 ms (xelulu_fw: 4): room for the screen pushes between reads
  cc.dma_frame_num = BLOCK;
  i2s_std_config_t sc = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATES[0]),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {I2S_GPIO_UNUSED, (gpio_num_t)PIN_MIC_BCLK, (gpio_num_t)PIN_MIC_WS,
                 I2S_GPIO_UNUSED, (gpio_num_t)PIN_MIC_DIN, {false, false, false}},
  };
  if (MSB_FORMAT) sc.slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
  if (ONLY) {
    sc.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    sc.slot_cfg.slot_mask = ONLY == 1 ? I2S_STD_SLOT_LEFT : I2S_STD_SLOT_RIGHT;
  }
  return i2s_new_channel(&cc, nullptr, &rx) == ESP_OK && i2s_channel_init_std_mode(rx, &sc) == ESP_OK &&
         i2s_channel_enable(rx) == ESP_OK;
}
static float hb0, hb1, hb2, ha1, ha2, hz1[2], hz2[2];   // the HPF_HZ high-pass (RBJ cookbook, Q 0.707) + its state
static void setRate(uint32_t hz) {   // the SPH0645 runs on BCLK = 64 x rate
  float w = 2 * PI * HPF_HZ / hz, cw = cosf(w), al = sinf(w) / (2 * 0.7071f), a0 = 1 + al;
  hb0 = hb2 = (1 + cw) / 2 / a0;
  hb1 = -(1 + cw) / a0;
  ha1 = -2 * cw / a0;
  ha2 = (1 - al) / a0;
  hz1[0] = hz1[1] = hz2[0] = hz2[1] = 0;
  i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(hz);
  i2s_channel_disable(rx);
  i2s_channel_reconfig_std_clock(rx, &clk);
  i2s_channel_enable(rx);
  char t[28];
  static const char* const TITLE[] = {"TEST 9", "L ONLY", "R ONLY"};
  snprintf(t, sizeof t, "%s  %u kHz  BCLK %.1fM", TITLE[ONLY], (unsigned)(hz / 1000), hz * 64 / 1e6);
  row(0, 2, C_CYAN, t);
}

static int32_t snap[8];   // the last block's first frames, raw (RAW_LOG)
static void measure(Chan ch[2]) {
  static int32_t raw[BLOCK * 2];   // L, R, L, R... (ONLY: the one slot's samples)
  static int32_t prev[2];
  double sum[2] = {}, sq[2] = {}, hq[2] = {};
  int32_t lo[2] = {INT32_MAX, INT32_MAX}, hi[2] = {INT32_MIN, INT32_MIN};
  int moved[2] = {};
  size_t got;
  for (int b = 0; b < WIN; b++) {
    i2s_channel_read(rx, raw, BLOCK * CH * 4, &got, portMAX_DELAY);
    for (int i = 0; i < BLOCK; i++)
      for (int k = 0; k < CH; k++) {
        int c = ONLY ? ONLY - 1 : k;               // the slot this sample came from
        int32_t v = raw[CH * i + k] >> 14;         // 18-bit sample, top-aligned in the 32-bit slot
        moved[c] += ((v ^ prev[c]) & 0xFF) != 0;   // a live mic's low bits churn every sample (its own noise)
        prev[c] = v;
        sum[c] += v;
        sq[c] += (double)v * v;
        float y = hb0 * v + hz1[c];                // the high-pass, transposed direct form II
        hz1[c] = hb1 * v - ha1 * y + hz2[c];
        hz2[c] = hb2 * v - ha2 * y;
        hq[c] += (double)y * y;
        lo[c] = min(lo[c], v);
        hi[c] = max(hi[c], v);
      }
  }
  memcpy(snap, raw, sizeof snap);
  const float N = WIN * BLOCK, FS = 131072;
  for (int c = 0; c < 2; c++) {
    float dc = sum[c] / N;
    double var = sq[c] / N - (sum[c] / N) * (sum[c] / N);   // DC off: the SPH0645 sits well off zero
    ch[c].rms = 10 * log10f(max(var, 0.0) / (FS * FS) + 1e-12f);
    ch[c].peak = 20 * log10f(max(hi[c] - dc, dc - lo[c]) / FS + 1e-6f);
    ch[c].dc = dc;
    ch[c].hp = 10 * log10f(hq[c] / N / (FS * FS) + 1e-12f);
    ch[c].dead = moved[c] < WIN * BLOCK / 2;   // flat, or only stray high bits: a floating line, no mic on it
  }
}

// ============================================================
static const char* const SIDE[] = {"-", "LEFT", "RIGHT", "SAME", "DEAD"};
static const char* const VERDICT[] = {"  - quiet -", "LEFT louder", "RIGHT louder", "SAME level", "a mic is DEAD"};
static const uint16_t VERDICT_COLOR[] = {C_GREY, C_GREEN, C_GREEN, C_YELLOW, C_RED};

void setup() {
  for (uint8_t p : MOTOR_PINS) {   // FIRST: wheels still
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);   // never block on a console nobody is reading
  tftBegin();
  row(40, 2, C_GREY, "scratch/blow on ONE mic");
  if (ONLY) row(ONLY == 1 ? 140 : 90, 3, C_GREY, ONLY == 1 ? "R  not read" : "L  not read");
  bool up = micBegin();
  Serial.printf("[test9] mic I2S %s. Per channel: rms dBFS, peak dBFS, DC; then the louder side\n", up ? "ok" : "FAILED");
  if (!up) {
    row(195, 3, C_RED, "I2S FAILED");
    for (;;) delay(1000);
  }
  if (PULL_UP) gpio_pullup_en((gpio_num_t)PIN_MIC_DIN);   // the pad only: the I2S routing stays
  setRate(RATES[0]);
}

void loop() {
  static float hold[2] = {-120, -120};
  static uint32_t holdAt[2], sideAt;
  static int shown, drawn = -1;
  static uint8_t n, ri;
  static uint32_t rateAt;
  Chan ch[2];
  measure(ch);
  uint32_t now = millis();
  if (now - rateAt >= RATE_MS) {   // next clock
    rateAt = now;
    ri = (ri + 1) % (sizeof RATES / sizeof *RATES);
    setRate(RATES[ri]);
  }
  for (int c = 0; c < 2; c++) {
    if (ONLY && c != ONLY - 1) continue;   // that slot isn't read
    if (ch[c].peak > hold[c] || now - holdAt[c] > 1500) { hold[c] = ch[c].peak; holdAt[c] = now; }
    bar(c ? 140 : 90, c ? "R" : "L", ch[c].rms, hold[c], ch[c].dead);
  }
  int side = 0;   // which mic hears this reading louder: 0 none, 1 left, 2 right, 3 same, 4 can't tell: a dead slot
  if (!ONLY) {
    if (ch[0].dead || ch[1].dead) side = 4;
    else if (max(ch[0].rms, ch[1].rms) > ACTIVE_DBFS) {
      float d = ch[0].rms - ch[1].rms;
      side = d >= SIDE_DB ? 1 : d <= -SIDE_DB ? 2 : 3;
    }
    if (side) { shown = side; sideAt = now; }
    else if (now - sideAt > 1500) shown = 0;   // keep a verdict up 1.5 s after the sound
    if (shown != drawn) { drawn = shown; row(195, 3, VERDICT_COLOR[shown], VERDICT[shown]); }
  }
  if (++n % 2 == 0) {
    Serial.printf("%2uk", (unsigned)(RATES[ri] / 1000));
    for (int c = 0; c < 2; c++)
      if (ONLY && c != ONLY - 1) Serial.printf(" | %s off", c ? "R" : "L");
      else Serial.printf(" | %s %6.1f pk %6.1f dc %7.0f hp %6.1f%s", c ? "R" : "L", ch[c].rms, ch[c].peak, ch[c].dc,
                         ch[c].hp, ch[c].dead ? " DEAD" : "");
    if (RAW_LOG)
      for (int c = 0; c < CH; c++)
        Serial.printf(" | raw%s %08lx %08lx %08lx %08lx", CH == 1 ? "" : c ? "R" : "L",
                      (unsigned long)(uint32_t)snap[c], (unsigned long)(uint32_t)snap[CH + c],
                      (unsigned long)(uint32_t)snap[2 * CH + c], (unsigned long)(uint32_t)snap[3 * CH + c]);
    Serial.printf(" | %s\n", SIDE[side]);
  }
}
