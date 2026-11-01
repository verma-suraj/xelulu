// ============================================================
// Wav_Test — minimal WAV-from-flash playback check
// Plays arghh-aaaa.wav (converted to a C array) through the
// MAX98357A every 3 seconds. No display, no touch, no servos.
//
// Wiring needed (same as the robot):
//   MAX98357A BCLK -> GPIO26, LRC -> GPIO25, DIN -> GPIO19
//   VIN -> 5V, GND -> GND, speaker on + / -
// ============================================================
#include <Arduino.h>
#include "arghh_sound.h"

#define PIN_I2S_BCLK 26
#define PIN_I2S_LRC  25
#define PIN_I2S_DIN  19

#define SAMPLE_RATE 16000
#define VOLUME 0.9f          // 0.0 .. 1.0 (samples are near full scale already)
#define CHUNK 256

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  #include <ESP_I2S.h>
  I2SClass i2s;
#else
  #include <driver/i2s.h>
#endif

static void i2sWriteStereo(int16_t* stereo, size_t frames) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  uint8_t* p = (uint8_t*)stereo;
  size_t left = frames * 4;
  while (left) {
    size_t w = i2s.write(p, left);
    p += w; left -= w;
    if (!w) delay(1);
  }
#else
  size_t written;
  i2s_write(I2S_NUM_0, stereo, frames * 4, &written, portMAX_DELAY);
#endif
}

void playClip(const int16_t* pcm, uint32_t len) {
  static int16_t stereo[CHUNK * 2];
  uint32_t pos = 0;
  while (pos < len) {
    size_t n = min((uint32_t)CHUNK, len - pos);
    for (size_t i = 0; i < n; i++) {
      int16_t s = (int16_t)(pcm[pos + i] * VOLUME);
      stereo[2 * i] = s;
      stereo[2 * i + 1] = s;
    }
    i2sWriteStereo(stereo, n);
    pos += n;
  }
  memset(stereo, 0, sizeof(stereo));          // flush silence: no tail looping
  for (uint8_t i = 0; i < 6; i++) i2sWriteStereo(stereo, CHUNK);
}

void setup() {
  Serial.begin(115200);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  i2s.setPins(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DIN, -1, -1);
  i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
#else
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 6;
  cfg.dma_buf_len = CHUNK;
  cfg.tx_desc_auto_clear = true;
  i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
  i2s_pin_config_t pins = {};
  pins.bck_io_num = PIN_I2S_BCLK;
  pins.ws_io_num = PIN_I2S_LRC;
  pins.data_out_num = PIN_I2S_DIN;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  i2s_set_pin(I2S_NUM_0, &pins);
#endif
  Serial.println("Wav_Test ready - playing every 3 s");
}

void loop() {
  Serial.println("play");
  playClip(ARGHH_PCM, ARGHH_LEN);
  delay(3000);
}
