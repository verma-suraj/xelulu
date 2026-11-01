// ============================================================
// TEST 4 - mic + speaker: 2x SPH0645 mics (stereo) + MAX98357 amp + speaker, on the robot's screen.
// Runs by itself after flashing (battery ON):
//   1. mics alive: each stereo slot (L = SEL GND, R = SEL 3.3 V) must carry a live signal
//   2. speaker: 3 notes you should hear
//   3. loopback: a 1 kHz tone the mics must hear = amp + speaker + mics work together
//   4. stereo: tap the LEFT mic, then the RIGHT one; the tapped side must be the louder one
// Then live L/R level bars. Press BOOT (GPIO0) to run the test again.
// Doesn't touch the servo pins 19/20, so the native USB (COM7) stays up: log on `Serial`, 115200.
//
// FQBN esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,CDCOnBoot=cdc,USBMode=hwcdc,FlashMode=qio
// Libraries: Adafruit GFX + Adafruit ST7735 and ST7789 (display set up as Display_test_code).
// ============================================================
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ESP_I2S.h>

// ---- pins: Xelulu_Wiring.md §3-5, §11 ----
#define PIN_TFT_SCLK   12
#define PIN_TFT_MOSI   11
#define PIN_TFT_CS     10
#define PIN_TFT_DC     14
#define PIN_TFT_RST    21
#define PIN_TFT_EN     45    // backlight, HIGH = on
#define PIN_MIC_BCLK    4    // both SPH0645
#define PIN_MIC_WS      5
#define PIN_MIC_DIN     6
#define PIN_SPK_BCLK   15    // MAX98357, SD on 3.3 V
#define PIN_SPK_WS     16
#define PIN_SPK_DOUT    7
#define PIN_BOOT        0    // the BOOT button: run again
static const uint8_t MOTOR_PINS[] = {1, 2, 42, 41, 13, 47};   // TB6612 STBY is tied high: keep these LOW

// ---- knobs ----
#define SPK_VOLUME  0.25f   // 0..1 of full scale; GAIN pin open = 9 dB on the amp
#define TONE_HZ     1000    // loopback tone: an exact Goertzel bin at 32 kHz / 256
#define HEARD_DB    10      // the mics must hear the tone this far above silence
#define TAP_DB      15      // a tap must be this loud over the room...
#define SIDE_DB     4       // ...and this much louder on the tapped side

#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_YELLOW 0xFFE0
#define C_CYAN   0x07FF
#define C_GREEN  0x07E0
#define C_RED    0xF800
#define C_GREY   0x4208

struct MicSum { double pow[2], tone[2]; uint32_t n, flat[2]; };   // what the mics heard since the last micTake()

// ============================================================ display: ST7789 320x240 landscape
static Adafruit_ST7789 tft(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
static GFXcanvas16 strip(320, 20);   // rows are drawn here and pushed in one go: no flicker

static void row(int y, uint16_t color, const char* fmt, ...) {   // one row of big text, 26 characters
  char b[40];
  va_list a;
  va_start(a, fmt);
  vsnprintf(b, sizeof b, fmt, a);
  va_end(a);
  strip.fillScreen(C_BLACK);
  strip.setTextSize(2);
  strip.setTextColor(color);
  strip.setCursor(4, 2);
  strip.print(b);
  tft.drawRGBBitmap(0, y, strip.getBuffer(), 320, 20);
}
static void tftBegin() {   // Display_test_code's sequence, step for step
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
  tft.setRotation(1);
  tft.fillScreen(C_BLACK);
}
static void status(const char* l1, const char* l2) {   // what is happening: two yellow rows + the log
  Serial.printf(">> %s %s\n", l1, l2);
  row(30, C_YELLOW, "%s", l1);
  row(52, C_YELLOW, "%s", l2);
}

// ============================================================ audio: mics on I2S0 (stereo), amp on I2S1
// Two tasks on core 0: one keeps reading the mics, one keeps the amp fed (silence unless toneHz is set).
#define AUDIO_HZ 32000   // SPH0645 wants a 1.024-4.096 MHz BCLK: 32 kHz x 64 bits = 2.048 MHz
#define BLOCK    256
static I2SClass mic(I2S_NUM_0), spk(I2S_NUM_1);
static bool micUp, spkUp;
static volatile float toneHz;              // 0 = silence
static float micDb[2] = {-120, -120};      // level of the last block, dBFS
static bool micFlat[2];                    // last block was one value over and over: nobody drives that slot
static MicSum micSum;
static portMUX_TYPE micMux = portMUX_INITIALIZER_UNLOCKED;
static const float FS2 = 131072.0f * 131072.0f / 2;   // mean square of a full-scale 18-bit sine

static float db(double meanSq) { return 10 * log10(meanSq / FS2 + 1e-12); }
static MicSum micTake() {
  taskENTER_CRITICAL(&micMux);
  MicSum s = micSum;
  micSum = {};
  taskEXIT_CRITICAL(&micMux);
  if (!s.n) s.n = 1;        // nothing yet: all zeros, reads as silence
  return s;
}
static void micTask(void*) {
  static int32_t raw[BLOCK * 2];   // L, R, L, R...
  const float k = 2 * cosf(2 * PI * TONE_HZ / AUDIO_HZ);
  for (;;) {
    if (mic.readBytes((char*)raw, sizeof raw) != sizeof raw) { delay(10); continue; }
    float pow[2], tone[2];
    bool flat[2];
    for (int c = 0; c < 2; c++) {
      float mean = 0;
      flat[c] = true;
      for (int i = 0; i < BLOCK; i++) {
        mean += raw[2 * i + c] >> 14;   // 18-bit sample, top-aligned in the 32-bit slot
        flat[c] &= raw[2 * i + c] == raw[c];
      }
      mean /= BLOCK;
      float p = 0, s1 = 0, s2 = 0;
      for (int i = 0; i < BLOCK; i++) {
        float x = (raw[2 * i + c] >> 14) - mean, s0 = x + k * s1 - s2;   // DC off; Goertzel at TONE_HZ
        p += x * x;
        s2 = s1;
        s1 = s0;
      }
      pow[c] = p / BLOCK;
      tone[c] = (s1 * s1 + s2 * s2 - k * s1 * s2) / (BLOCK * BLOCK / 2.0f);   // same scale as pow
      micDb[c] = db(pow[c]);
      micFlat[c] = flat[c];
    }
    taskENTER_CRITICAL(&micMux);
    for (int c = 0; c < 2; c++) {
      micSum.pow[c] += pow[c];
      micSum.tone[c] += tone[c];
      micSum.flat[c] += flat[c];
    }
    micSum.n++;
    taskEXIT_CRITICAL(&micMux);
  }
}
static void spkTask(void*) {
  static int16_t buf[BLOCK * 2];   // same sample on both slots: SD on 3.3 V plays the left one
  float ph = 0;
  for (;;) {
    float f = toneHz, a = f > 0 ? SPK_VOLUME * 32767 : 0;
    for (int i = 0; i < BLOCK; i++) {
      buf[2 * i] = buf[2 * i + 1] = a * sinf(ph);
      ph += 2 * PI * f / AUDIO_HZ;
      if (ph > 2 * PI) ph -= 2 * PI;
    }
    spk.write((uint8_t*)buf, sizeof buf);   // blocks until the DMA has room: paces the task
  }
}
static void audioBegin() {
  mic.setPins(PIN_MIC_BCLK, PIN_MIC_WS, -1, PIN_MIC_DIN);
  micUp = mic.begin(I2S_MODE_STD, AUDIO_HZ, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
  spk.setPins(PIN_SPK_BCLK, PIN_SPK_WS, PIN_SPK_DOUT);
  spkUp = spk.begin(I2S_MODE_STD, AUDIO_HZ, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  if (micUp) xTaskCreatePinnedToCore(micTask, "mic", 4096, nullptr, 2, nullptr, 0);
  if (spkUp) xTaskCreatePinnedToCore(spkTask, "spk", 4096, nullptr, 2, nullptr, 0);
  Serial.printf("[audio] mic I2S %s  speaker I2S %s\n", micUp ? "ok" : "FAILED", spkUp ? "ok" : "FAILED");
}

// ============================================================ live: L/R level bars + numbers, -90..-20 dBFS
static char result[28];   // the last verdict, kept on screen
static bool resultOk;

static void drawBar(uint8_t c) {   // dark red = nobody drives that slot
  int y = 100 + c * 50;
  bool dead = !micUp || micFlat[c];
  strip.fillScreen(C_BLACK);
  strip.setTextSize(2);
  strip.setTextColor(C_WHITE);
  strip.setCursor(2, 2);
  strip.print(c ? "R" : "L");
  strip.drawRect(18, 0, 220, 20, C_GREY);
  if (dead) strip.fillRect(20, 2, 216, 16, 0x8000);
  else strip.fillRect(20, 2, constrain((int)((micDb[c] + 90) * 216 / 70), 0, 216), 16, C_GREEN);
  strip.setCursor(246, 2);
  if (dead) strip.print("DEAD");
  else strip.printf("%4.0f", micDb[c]);
  tft.drawRGBBitmap(0, y, strip.getBuffer(), 320, 20);
}
static void liveTick() {
  static uint32_t drawAt, printAt;
  static uint8_t next;
  uint32_t now = millis();
  if (now - drawAt >= 30) {
    drawAt = now;
    if (next < 2) drawBar(next);
    else if (*result) row(210, resultOk ? C_GREEN : C_RED, "%s", result);
    next = (next + 1) % 3;
  }
  if (now - printAt >= 500) {
    printAt = now;
    Serial.printf("mic L %s R %s dBFS\n", micFlat[0] ? "--" : String(micDb[0], 0).c_str(),
                  micFlat[1] ? "--" : String(micDb[1], 0).c_str());
  }
}
static void wait(uint32_t ms) {   // a delay that keeps the bars moving
  for (uint32_t t0 = millis(); millis() - t0 < ms;) {
    liveTick();
    delay(1);
  }
}

// ============================================================ the test
// Tap one mic for 6 s: the loudest moment must be TAP_DB over the room, and SIDE_DB louder on
// the tapped side than on the other. 1 = right side, 0 = wrong side, -1 = nothing / can't tell.
static int tapTest(uint8_t side) {
  static const char* const SIDE[2] = {"LEFT", "RIGHT"};
  char l1[28], l2[28];
  snprintf(l1, sizeof l1, "Tap the %s mic", SIDE[side]);
  status(l1, "(scratch / blow on it)");
  micTake();
  wait(800);
  MicSum q = micTake();
  float room[2] = {db(q.pow[0] / q.n), db(q.pow[1] / q.n)}, rise = 0, diff = 0;
  for (uint32_t t0 = millis(); millis() - t0 < 6000;) {
    wait(100);
    MicSum w = micTake();
    float lv[2] = {db(w.pow[0] / w.n), db(w.pow[1] / w.n)};
    float r = max(lv[0] - room[0], lv[1] - room[1]);
    if (r > rise) { rise = r; diff = lv[side] - lv[1 - side]; }
  }
  int res = rise < TAP_DB ? -1 : diff >= SIDE_DB ? 1 : diff <= -SIDE_DB ? 0 : -1;
  if (rise < TAP_DB) snprintf(l2, sizeof l2, "no tap heard");
  else snprintf(l2, sizeof l2, res == 1 ? "OK: %+.0f dB louder" : res == 0 ? "WRONG side! %+.0f dB" : "same both sides %+.0f", diff);
  status(l1, l2);
  Serial.printf("   %s: loudest %+.0f dB over the room, tapped side %+.0f dB vs the other\n", l1, rise, diff);
  wait(2000);
  return res;
}
static void runAudio() {
  char l1[28];
  *result = 0;
  row(210, C_BLACK, "");
  if (!micUp || !spkUp) {
    status("I2S start FAILED", micUp ? "speaker port" : "mic port");
    strcpy(result, "I2S FAILED");
    resultOk = false;
    return;
  }
  // 1. both mics alive: each slot must carry a changing signal, at a sane level
  toneHz = 0;
  status("Mics: listening...", "quiet please");
  wait(300);
  micTake();
  wait(1000);
  MicSum q = micTake();
  bool alive[2];
  for (int c = 0; c < 2; c++) {
    float lv = db(q.pow[c] / q.n);
    alive[c] = q.flat[c] < q.n / 2 && lv < -20;   // flat = nobody on that slot; very loud = two mics fighting
    Serial.printf("   mic %s: room %.0f dBFS, %s\n", c ? "R" : "L", lv, alive[c] ? "alive" : "DEAD / garbled");
  }
  snprintf(l1, sizeof l1, "Mic L %s  R %s", alive[0] ? "ok" : "DEAD", alive[1] ? "ok" : "DEAD");
  status(l1, alive[0] && alive[1] ? "" : "check SEL + DOUT wires");
  wait(2000);
  // 2. the chime: three notes you should hear
  status("Speaker: 3 notes", "listen...");
  for (float f : {523.f, 659.f, 784.f}) { toneHz = f; wait(250); }
  toneHz = 0;
  wait(400);
  // 3. loopback: the mics must hear the TONE_HZ tone
  status("Speaker: 1 kHz tone", "the mics listen...");
  micTake();
  wait(500);
  MicSum s = micTake();
  toneHz = TONE_HZ;
  wait(200);
  micTake();
  wait(1000);
  MicSum t = micTake();
  toneHz = 0;
  float gain[2];
  for (int c = 0; c < 2; c++) gain[c] = db(t.tone[c] / t.n) - db(s.tone[c] / s.n);
  bool heard = (alive[0] && gain[0] >= HEARD_DB) || (alive[1] && gain[1] >= HEARD_DB);
  snprintf(l1, sizeof l1, "Tone heard L%+.0f R%+.0f dB", gain[0], gain[1]);
  status(l1, heard ? "speaker + amp OK" : alive[0] || alive[1] ? "NOT heard: amp/speaker?" : "no mic: did you hear?");
  Serial.printf("   1 kHz tone over silence: L %+.0f dB  R %+.0f dB -> %s\n", gain[0], gain[1], heard ? "heard" : "NOT heard");
  wait(2500);
  // 4. stereo: tap each mic, the tapped side must be the louder one
  int tl = tapTest(0), tr = tapTest(1);
  bool stereo = tl == 1 && tr == 1, swapped = tl == 0 && tr == 0;
  snprintf(result, sizeof result, "Mic %s%s Spk %s L/R %s", alive[0] ? "L" : "-", alive[1] ? "R" : "-",
           heard ? "ok" : "BAD", stereo ? "ok" : swapped ? "SWAP" : "BAD");
  resultOk = alive[0] && alive[1] && heard && stereo;
  status(resultOk ? "AUDIO ALL OK" : "AUDIO PROBLEM", swapped ? "L/R swapped: swap SELs" : "BOOT = test again");
  Serial.printf("[audio] %s\n", result);
}

void setup() {
  for (uint8_t p : MOTOR_PINS) {   // FIRST: wheels still
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
  pinMode(PIN_BOOT, INPUT_PULLUP);
  Serial.begin(115200);
  tftBegin();
  row(0, C_CYAN, "TEST 4  MIC + SPEAKER");
  row(80, C_GREY, "Mic levels (dBFS)");
  audioBegin();
  runAudio();
}

void loop() {
  liveTick();
  if (digitalRead(PIN_BOOT) == LOW) runAudio();
  delay(1);
}
