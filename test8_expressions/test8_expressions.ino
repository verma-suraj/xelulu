// TEST 8 - the expression sheet (FW-SOT §7.4): all 48 faces with their sounds, stepped by touch.
//   HEAD  = next expression       BELLY = play this one again       BACK = previous expression
// Face, glow and sound code are xelulu_fw's own, copied unchanged into src/ (2026-10-31):
// Face.cpp/Glow.cpp (EilikFace port), Sfx.cpp + ClipsData.h (Eillik synth + WAV clips, 50 % clip pick).
// Re-copy them when xelulu_fw's face or sound changes. Touch = xelulu_fw Touch.cpp's setup with
// Config.h's calibrated thresholds. The expression number is shown top-left; the name is on the console.
// Hands OFF while it boots (the touch chip learns its baselines). Battery ON.
// FQBN esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,CDCOnBoot=cdc,USBMode=hwcdc,FlashMode=qio
// Libraries: TFT_eSPI 2.5.43 (configured by build_opt.h here). Console = native USB, 115200.
// build_opt.h uses USE_HSPI_PORT, not xelulu_fw's USE_FSPI_PORT: on the S3 the FSPI branch turns
// TFT_MISO -1 into the MOSI pin, core 3.x then stops the SPI bus inside spi.begin(), and tft.init()
// hangs on its first command (blank screen). SPI at 27 MHz (= 26.67, the proven Adafruit test's speed).
// After editing build_opt.h, build with --clean: the build cache doesn't see changes inside it.
#include <Wire.h>
#include <TFT_eSPI.h>
#include "driver/i2s_std.h"
#include "Pins.h"
#include "src/State.h"
#include "src/store/Store.h"
#include "src/face/Face.h"
#include "src/audio/Sfx.h"

#define VOLUME   100    // 0-100 = xelulu_fw's default level. Its curve: -40 + 0.4 x level dB, capped at cal.volMaxDb
#define RATE     16000
#define BLOCK    160    // 10 ms
#define MPR      0x5A
static const uint8_t MOTOR_PINS[] = {1, 2, 42, 41, 13, 47};   // TB6612 STBY is tied high: keep these LOW

Cal cal;                // Config.h defaults (what xelulu_fw uses until `cal save`)
Shared S = {};          // nobody uploading: every face is allowed

static FaceId cur = FACE_SMILE;
static bool touchUp;
static uint8_t lastSt;

// Face.cpp calls this before every push: the number goes on the canvas, near the top-left that the zoom shows
void faceOverlay(TFT_eSprite& s) {
  s.setTextColor(TFT_WHITE, TFT_BLACK);
  s.setTextDatum(TL_DATUM);
  s.drawNumber(cur + 1, 18, 44, 2);
}

// ---- speaker: xelulu_fw Audio.cpp's I2S1 TX, SFX channel only ----
static i2s_chan_handle_t tx;
static float gain;
static void audioTask(void*) {
  static int16_t sfx[BLOCK], out[BLOCK * 2];
  size_t w;
  for (;;) {
    sfxRender(sfx, BLOCK);
    for (int i = 0; i < BLOCK; i++) out[2 * i] = out[2 * i + 1] = (int16_t)constrain(sfx[i] * gain, -32768.f, 32767.f);
    i2s_channel_write(tx, out, sizeof out, &w, portMAX_DELAY);   // paces this task at 10 ms a block
  }
}
static void audioBegin() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  cc.dma_desc_num = 4;
  cc.dma_frame_num = BLOCK;
  cc.auto_clear = true;
  i2s_new_channel(&cc, &tx, nullptr);
  i2s_std_config_t sc = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {I2S_GPIO_UNUSED, (gpio_num_t)PIN_SPK_BCLK, (gpio_num_t)PIN_SPK_LRC, (gpio_num_t)PIN_SPK_DIN,
                 I2S_GPIO_UNUSED, {false, false, false}},
  };
  i2s_channel_init_std_mode(tx, &sc);
  i2s_channel_enable(tx);
  gain = powf(10.0f, min(-40.0f + 0.4f * VOLUME, (float)cal.volMaxDb) / 20.0f);
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 22, nullptr, 1);
}

// ---- touch: xelulu_fw Touch.cpp's MPR121 setup ----
static bool i2cWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}
static bool i2cRead(uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(MPR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom((uint8_t)MPR, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}
static bool touchBegin() {
  i2cWrite(0x80, 0x63);
  delay(1);
  i2cWrite(0x5E, 0x00);
  uint8_t c2 = 0;
  if (!i2cRead(0x5D, &c2, 1) || c2 != 0x24) return false;
  for (uint8_t z = 0; z < 3; z++) {   // head, back, belly
    i2cWrite(0x41 + 2 * cal.touchEle[z], cal.touchThr[z]);
    i2cWrite(0x42 + 2 * cal.touchEle[z], cal.relThr[z]);
  }
  static const uint8_t REGS[][2] = {
    {0x2B, 0x01}, {0x2C, 0x01}, {0x2D, 0x0E}, {0x2E, 0x00}, {0x2F, 0x01}, {0x30, 0x01},   // slow falling
    {0x31, 0xFF}, {0x32, 0x02}, {0x33, 0x00}, {0x34, 0x00}, {0x35, 0x00}, {0x5B, 0x00},   // filter
    {0x5C, 0x10}, {0x5D, 0x20}, {0x7B, 0x0B}, {0x7D, 200}, {0x7E, 130}, {0x7F, 180},
  };
  for (auto& r : REGS) i2cWrite(r[0], r[1]);
  return i2cWrite(0x5E, 0x83);   // run ELE0-2, baseline from the first reading
}

static void show(FaceId f) {   // face + its sound from the start, as xelulu_fw's react() does
  cur = f;
  faceSet(f);
  sfxPlayFace(f);
}

void setup() {
  for (uint8_t p : MOTOR_PINS) {   // FIRST: wheels still
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);   // never block on a USB console nobody is reading (as xelulu_fw)
  delay(500);   // Display_test_code: EN high after the panel has had power for a moment
  pinMode(PIN_TFT_EN, OUTPUT);
  digitalWrite(PIN_TFT_EN, HIGH);
  faceBegin();
  audioBegin();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  Wire.setTimeOut(25);
  delay(1500);   // hands off: the touch chip learns its baselines at start
  touchUp = touchBegin();
  Serial.printf("[test8] touch %s. HEAD next, BELLY again, BACK previous\n", touchUp ? "ok" : "MISSING (0x5A)");
  show(FACE_SMILE);
}

void loop() {
  uint32_t now = millis();
  uint8_t st;
  if (touchUp && i2cRead(0x00, &st, 1)) {
    uint8_t down = st & ~lastSt;   // newly touched: ELE0 head, ELE1 back, ELE2 belly
    lastSt = st;
    if (down & 1) show((FaceId)((cur + 1) % FACE_COUNT));
    else if (down & 4) show(cur);
    else if (down & 2) show((FaceId)((cur + FACE_COUNT - 1) % FACE_COUNT));
  }
  faceUpdate(now);
  static uint32_t beat;
  if (now - beat >= 5000) {   // proof of life on the console: frames are going out (fps > 0)
    beat = now;
    Serial.printf("[test8] face %d %s  fps %u\n", cur + 1, FACE_NAMES[cur], faceFps());
  }
  delay(1);
}
