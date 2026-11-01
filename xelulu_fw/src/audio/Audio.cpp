#include "Audio.h"
#include "Sfx.h"
#include "Turn.h"
#include "../../Pins.h"
#include "../State.h"
#include "../store/Store.h"
#include "driver/i2s_std.h"
#include "esp_task_wdt.h"

#define RATE    16000
#define BLOCK   160                     // 10 ms
#define TX_DESC 4                       // TX DMA depth: 4 x 10 ms

static i2s_chan_handle_t rx = nullptr, tx = nullptr;
static volatile float masterGain = 0.1f;
static volatile bool speakerOn = true, speakerWant = true;
static volatile uint32_t written = 0;   // samples handed to I2S1
static volatile float env = 0, levL = -120, levR = -120;

void audioVolume(uint8_t level) {
  float db = -40.0f + 0.4f * min<uint8_t>(level, 100);
  if (db > cal.volMaxDb) db = cal.volMaxDb;
  masterGain = powf(10.0f, db / 20.0f);
}

void audioSpeaker(bool on) { speakerWant = on; }

uint32_t audioPlayheadMs() {
  uint32_t w = written, q = TX_DESC * BLOCK;   // samples still queued in DMA have not been heard yet
  return w > q ? (w - q) / (RATE / 1000) : 0;
}

float audioEnvelope() { return env; }

void audioMicLevels(float& l, float& r) { l = levL; r = levR; }

static void initTx() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  cc.dma_desc_num = TX_DESC;
  cc.dma_frame_num = BLOCK;
  cc.auto_clear = true;                 // an underrun plays zeros, never old audio
  i2s_new_channel(&cc, &tx, nullptr);
  i2s_std_config_t sc = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {I2S_GPIO_UNUSED, (gpio_num_t)PIN_SPK_BCLK, (gpio_num_t)PIN_SPK_LRC,
                 (gpio_num_t)PIN_SPK_DIN, I2S_GPIO_UNUSED, {false, false, false}},
  };
  i2s_channel_init_std_mode(tx, &sc);
  i2s_channel_enable(tx);
}

static void initRx() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.dma_desc_num = 4;
  cc.dma_frame_num = BLOCK;
  i2s_new_channel(&cc, nullptr, &rx);
  i2s_std_config_t sc = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {I2S_GPIO_UNUSED, (gpio_num_t)PIN_MIC_BCLK, (gpio_num_t)PIN_MIC_WS,
                 I2S_GPIO_UNUSED, (gpio_num_t)PIN_MIC_DATA, {false, false, false}},
  };
  // the SPH0645 on ESP32 sometimes needs MSB format: samples look bit-shifted otherwise (§2.5)
  if (cal.micMsbFormat) sc.slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
  i2s_channel_init_std_mode(rx, &sc);
  i2s_channel_enable(rx);
}

// 32-bit slots, 18 valid bits: sample >> 14, then to int16 with the mic gain (§2.5)
static void micBlock(const int32_t* raw, int frames) {
  static float accL = 0, accR = 0;
  static uint32_t liveAt = 0, bootAt = millis();
  static int32_t prev = 0;
  float g = powf(10.0f, cal.micGainDb / 20.0f) / 4.0f;   // /4: 18 -> 16 bits
  double sl = 0, sr = 0;
  bool moving = false;
  static int16_t mono[BLOCK];
  for (int i = 0; i < frames; i++) {
    float l = (raw[2 * i] >> 14) * g, r = (raw[2 * i + 1] >> 14) * g;
    sl += l * l; sr += r * r;
    float m = (l + r) * 0.5f;                   // ponytail: L+R average until the AFE's BSS takes both mics
    mono[i] = m > 32767 ? 32767 : m < -32768 ? -32768 : (int16_t)m;
    if (raw[2 * i] != prev) moving = true;
    prev = raw[2 * i];
  }
  float dl = 10 * log10f(sl / frames / (32768.0f * 32768.0f) + 1e-12f);
  float dr = 10 * log10f(sr / frames / (32768.0f * 32768.0f) + 1e-12f);
  accL += (dl - accL) * 0.05f;                        // ~200 ms smoothing
  accR += (dr - accR) * 0.05f;
  levL = accL; levR = accR;
  turnMicIn(mono, frames);
  // a DATA line that never changes is a dead mic, not a quiet room
  uint32_t now = millis();
  if (moving) { liveAt = now; if (faultOn(F_MIC_SILENT)) faultClear(F_MIC_SILENT); }
  else if (now - max(liveAt, bootAt) > 5000) faultSet(F_MIC_SILENT);
}

static void audioTask(void*) {
  esp_task_wdt_add(nullptr);
  static int32_t mic[BLOCK * 2];
  static int16_t sfx[BLOCK], strm[BLOCK], out[BLOCK * 2];
  const float duck = powf(10.0f, -12 / 20.0f);  // SFX under the stream: -12 dB (§5.4)
  // prefill: two blocks of silence give the TX DMA some slack
  memset(out, 0, sizeof out);
  size_t w;
  for (int i = 0; i < 2; i++) i2s_channel_write(tx, out, sizeof out, &w, pdMS_TO_TICKS(50));
  for (;;) {
    esp_task_wdt_reset();
    size_t got = 0;
    i2s_channel_read(rx, mic, sizeof mic, &got, pdMS_TO_TICKS(100));
    if (got) micBlock(mic, got / 8);

    if (speakerWant != speakerOn) {
      speakerOn = speakerWant;
      if (speakerOn) i2s_channel_enable(tx); else i2s_channel_disable(tx);
    }

    sfxRender(sfx, BLOCK);
    bool streaming = turnStreamOut(strm, BLOCK);
    float sfxGain = streaming ? duck : 1.0f;
    float gain = masterGain;
    if (S.wheelsAccel) gain *= powf(10.0f, -cal.driveDuckDb / 20.0f);   // §2.3: -6 dB while accelerating
    float peak = 0;
    for (int i = 0; i < BLOCK; i++) {
      float v = (sfx[i] * sfxGain + strm[i]) * gain;
      if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
      out[2 * i] = out[2 * i + 1] = (int16_t)v;   // same mono sample in both slots (as Eillik)
      peak = max(peak, fabsf(v));
    }
    // mouth envelope: attack 15 ms, release 90 ms, gate -45 dBFS (§5.11)
    float target = peak < 184 ? 0 : min(1.0f, peak / 16000.0f);
    env += (target - env) * (target > env ? 10.0f / 15 : 10.0f / 90);

    if (speakerOn) {
      i2s_channel_write(tx, out, sizeof out, &w, pdMS_TO_TICKS(50));
      written += BLOCK;
    }
  }
}

void audioBegin() {
  initTx();
  initRx();
  audioVolume(settings.volume);
  // core 1 at priority 22 (§3.3): nothing else may starve the DMA
  xTaskCreatePinnedToCore(audioTask, "audio_io", 6144, nullptr, 22, nullptr, 1);
  Serial.println("[audio] I2S0 mics + I2S1 speaker at 16 kHz");
}
