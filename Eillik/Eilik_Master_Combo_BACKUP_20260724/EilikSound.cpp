#include "EilikSound.h"
#include "Pins.h"
#include "EilikClipsData.h"   // flash PCM arrays (only this file may include it)

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  #include <ESP_I2S.h>
  static I2SClass i2s;
#else
  #include <driver/i2s.h>
#endif

#define SAMPLE_RATE 16000
#define CHUNK 256      // samples rendered between queue checks (new sounds cut in fast)
#define SND_GAIN 90    // master volume knob, 30 = whisper, 120 = loud
// need even more? tie the MAX98357A GAIN pin to GND for +12 dB in hardware
#define BOOT_ID 200    // queue id for the startup jingle

// One sound = a list of segments. amp 0 = silent gap.
// wave: 0 sine (soft), 1 soft square (buzzy), 2 noise (static).
// vibHz/vibDep: optional vibrato (shimmer/wobble/boing effects).
struct Seg {
  uint16_t f0, f1, ms;
  uint8_t  amp, wave, vibHz, vibDep;
};

// ---- 38 effects, indexed by FaceExpr (Eilik_Sound_Generation_Prompt.md) ----
// Row 1: Joy & Excitement — major, ascending, fast bounce
static const Seg S_SMILE[]      = {{800,1500,180,150}};                                                     // quick upward chirp
static const Seg S_GIDDY[]      = {{1300,1400,70,150},{0,0,30,0},{1600,1700,70,150},{0,0,30,0},{1450,1350,70,145},{0,0,30,0},{1750,1850,90,150}};
static const Seg S_ECSTATIC[]   = {{1046,1046,60,140},{1175,1175,60,142},{1318,1318,60,144},{1480,1480,60,146},{1568,1568,60,148},{0,0,20,0},{1900,2100,70,160}};
static const Seg S_CHARMED[]    = {{880,880,220,130},{0,0,40,0},{1175,1175,320,135}};                       // two-tone chime
static const Seg S_SPARKLE[]    = {{1568,1568,70,140},{1976,1976,70,140},{2349,2349,70,140},{0,0,40,0},{2093,2637,110,135}};
static const Seg S_WINK[]       = {{600,900,110,150},{0,0,30,0},{1400,1500,80,150},{0,0,20,0},{1900,2300,50,160}};
static const Seg S_STARRY[]     = {{1300,1300,700,120,0,9,60}};                                             // shimmering vibrato
static const Seg S_LAUGH[]      = {{1000,860,90,150},{0,0,60,0},{980,840,90,145},{0,0,60,0},{950,820,90,140},{0,0,60,0},{920,800,110,135}};
static const Seg S_EAGER[]      = {{900,1300,110,145},{0,0,50,0},{1100,1600,130,150}};
static const Seg S_ANTICIP[]    = {{700,750,90,120},{0,0,160,0},{750,800,90,125},{0,0,120,0},{800,860,80,130},{0,0,80,0},{860,930,80,140},{0,0,50,0},{930,1050,70,150}};
// Row 2: Sadness & Anger — descending, dissonant/buzzy
static const Seg S_POUT[]       = {{700,520,300,120},{520,380,400,95}};                                     // digital sigh
static const Seg S_TEARY[]      = {{750,500,900,110,0,6,35}};                                               // wobbly descent
static const Seg S_CRYING[]     = {{1200,900,140,120},{0,0,350,0},{1100,800,150,110},{0,0,400,0},{1000,700,170,100}};
static const Seg S_DISAPP[]     = {{360,310,140,140},{310,255,220,130}};                                    // "womp"
static const Seg S_HEARTBRK[]   = {{1500,1500,180,140},{0,0,40,0},{380,300,350,140,1},{300,240,250,110,1}}; // bleep cracks to buzz
static const Seg S_GRUMPY[]     = {{320,305,100,150,1},{0,0,70,0},{300,285,100,150,1},{0,0,70,0},{280,265,140,150,1}};
static const Seg S_FURIOUS[]    = {{900,700,80,160,1},{0,0,30,0},{1200,1400,70,160,1},{0,0,25,0},{600,500,90,160,1},{0,0,30,0},{1500,1100,110,165,1}};
static const Seg S_FUMING[]     = {{300,300,50,140,1},{0,0,220,0},{300,300,50,140,1},{0,0,180,0},{300,300,50,145,1},{0,0,140,0},{300,300,50,150,1},{0,0,100,0},{0,0,180,100,2}};  // ticking -> static
static const Seg S_ANNOYED[]    = {{420,420,220,140}};                                                      // flat monotone
static const Seg S_FLUSHED[]    = {{800,900,60,130,1},{0,0,25,0},{1000,1150,55,135,1},{0,0,20,0},{1250,1450,55,140,1},{0,0,15,0},{1550,1800,60,150,1},{1900,2200,70,150,1}};
// Row 3: Curiosity, Playful & Affection — pitch bends, vocal inflections
static const Seg S_QUESTION[]   = {{600,650,160,120},{650,1200,180,145}};                                   // "hm?"
static const Seg S_INVESTIG[]   = {{1300,1250,120,130},{0,0,330,0},{1300,1250,120,125},{0,0,330,0},{1300,1250,120,120}};  // sonar pings
static const Seg S_THOUGHT[]    = {{700,700,160,120},{0,0,120,0},{950,950,160,120},{0,0,120,0},{700,700,160,115},{0,0,120,0},{950,950,160,115}};
static const Seg S_CONFUSED[]   = {{500,900,260,125},{900,500,260,125},{500,900,260,120},{900,450,300,110}};
static const Seg S_WONDER[]     = {{350,1300,450,130},{1300,1280,350,110}};                                 // slide up, hold softly
static const Seg S_PLAYFUL[]    = {{1100,1150,80,150},{0,0,40,0},{1300,1350,60,120},{0,0,90,0},{950,1000,70,140},{0,0,30,0},{1450,1500,90,155}};
static const Seg S_MISCHIEF[]   = {{600,620,90,60},{0,0,40,0},{750,780,90,85},{0,0,40,0},{900,940,90,110},{0,0,40,0},{1100,1150,90,140},{0,0,30,0},{1600,1900,80,160}};  // quiet -> loud
static const Seg S_CHEEKY[]     = {{1500,600,220,150,0,18,120},{550,480,120,110,0,12,60}};                  // springy boing
static const Seg S_DERP[]       = {{820,790,120,130},{0,0,90,0},{1237,1219,90,135},{0,0,150,0},{600,640,140,125},{0,0,60,0},{1490,1400,110,130}};
static const Seg S_LOVING[]     = {{600,600,110,140},{0,0,60,0},{450,450,140,130},{0,0,420,0},{600,600,110,135},{0,0,60,0},{450,450,140,125}};  // ba-bum ba-bum
// Row 4: Fatigue & Reactive — low energy or sudden bursts
static const Seg S_TIRED[]      = {{600,420,350,120},{420,300,450,80}};
static const Seg S_SLEEPING[]   = {{265,250,500,90},{0,0,350,0},{250,235,500,70}};                          // fading hums / snore
static const Seg S_YAWNING[]    = {{500,780,400,110,0,5,30},{780,300,700,90,0,5,35}};
static const Seg S_SLEEPY[]     = {{440,430,250,110},{0,0,180,0},{415,400,300,100},{0,0,220,0},{380,360,380,85}};
static const Seg S_SCARED[]     = {{1800,1850,45,130},{0,0,35,0},{1750,1800,45,130},{0,0,35,0},{1850,1900,45,130},{0,0,35,0},{1700,1750,45,125},{0,0,35,0},{1800,1850,60,120}};
static const Seg S_BORED[]      = {{500,490,260,120},{490,380,180,90}};                                     // unenthusiastic "meh"
static const Seg S_EMBARR[]     = {{900,1000,60,130},{1050,1150,60,135},{1200,1300,60,140},{1350,1500,60,145},{1550,1750,60,150}};  // trill, cut
static const Seg S_HUNGRY[]     = {{1400,1400,110,140},{0,0,50,0},{950,950,110,135},{0,0,120,0},{1400,1400,110,140},{0,0,50,0},{950,950,110,135}};

static const Seg S_BOOT[]       = {{392,392,150,150},{0,0,50,0},{523,523,150,150},{0,0,50,0},{659,784,240,160}};

struct SoundDef { const Seg* segs; uint8_t n; };
#define SND(s) {s, sizeof(s)/sizeof(Seg)}
static const SoundDef SOUNDS[FACE_COUNT] = {
  SND(S_SMILE), SND(S_GIDDY), SND(S_ECSTATIC), SND(S_CHARMED), SND(S_SPARKLE),
  SND(S_WINK), SND(S_STARRY), SND(S_LAUGH), SND(S_EAGER), SND(S_ANTICIP),
  SND(S_POUT), SND(S_TEARY), SND(S_CRYING), SND(S_DISAPP), SND(S_HEARTBRK),
  SND(S_GRUMPY), SND(S_FURIOUS), SND(S_FUMING), SND(S_ANNOYED), SND(S_FLUSHED),
  SND(S_QUESTION), SND(S_INVESTIG), SND(S_THOUGHT), SND(S_CONFUSED), SND(S_WONDER),
  SND(S_PLAYFUL), SND(S_MISCHIEF), SND(S_CHEEKY), SND(S_DERP), SND(S_LOVING),
  SND(S_TIRED), SND(S_SLEEPING), SND(S_YAWNING), SND(S_SLEEPY), SND(S_SCARED),
  SND(S_BORED), SND(S_EMBARR), SND(S_HUNGRY),
};
static const SoundDef SOUND_BOOT = SND(S_BOOT);

// ---- WAV clips (flash PCM) + face mapping ----
#define CLIP_VOLUME 0.9f
#define CLIP_ID_BASE 100          // queue ids: 0..37 synth, 100+ clips, 200 boot jingle
struct Clip { const int16_t* pcm; uint32_t len; };
static const Clip CLIPS[CLIP_COUNT] = {
  {PCM_BAHHH, PCM_BAHHH_LEN}, {PCM_HEHE, PCM_HEHE_LEN}, {PCM_HELLO, PCM_HELLO_LEN},
  {PCM_OI, PCM_OI_LEN}, {PCM_PIGYAWN, PCM_PIGYAWN_LEN}, {PCM_RISA, PCM_RISA_LEN},
  {PCM_SOARYN, PCM_SOARYN_LEN}, {PCM_SUS, PCM_SUS_LEN}, {PCM_WHAT, PCM_WHAT_LEN},
  {PCM_ARGHH, PCM_ARGHH_LEN},
};
// which faces have a clip voice (-1 = synth only), indexed by FaceExpr
// Requirements.md sound changes: giddy -> risa, cheeky tongue -> bahhh,
// silly derp -> oi (matched by emotion NAME).
static const int8_t CLIP_FOR_FACE[FACE_COUNT] = {
  /*SMILE*/-1, /*GIDDY*/CLIP_RISA, /*ECSTATIC*/-1, /*CHARMED*/-1, /*SPARKLE*/-1,
  /*WINK*/-1, /*STARRY*/CLIP_SOARYN, /*LAUGH*/CLIP_RISA, /*EAGER*/-1, /*ANTICIPATION*/-1,
  /*POUT*/-1, /*TEARY*/-1, /*CRYING*/-1, /*DISAPPOINTED*/-1, /*HEARTBROKEN*/-1,
  /*GRUMPY*/CLIP_BAHHH, /*FURIOUS*/CLIP_ARGHH, /*FUMING*/-1, /*ANNOYED*/CLIP_BAHHH, /*FLUSHED*/-1,
  /*QUESTIONING*/CLIP_SUS, /*INVESTIGATING*/CLIP_SUS, /*THOUGHTFUL*/-1, /*CONFUSED*/CLIP_WHAT, /*WONDER*/CLIP_OI,
  /*PLAYFUL*/CLIP_HEHE, /*MISCHIEVOUS*/CLIP_HEHE, /*CHEEKY*/CLIP_BAHHH, /*DERP*/CLIP_OI, /*LOVING*/-1,
  /*TIRED*/-1, /*SLEEPING*/-1, /*YAWNING*/CLIP_PIGYAWN, /*SLEEPY*/-1, /*SCARED*/-1,
  /*BORED*/-1, /*EMBARRASSED*/-1, /*HUNGRY*/-1,
};
// continuous faces: synth sound re-plays after this gap (ms) until a new
// sound is queued. 0 = play once. Clips never loop (a voice on repeat grates).
static const uint16_t LOOP_GAP_MS[FACE_COUNT] = {
  /*SMILE*/2600, /*GIDDY*/0, /*ECSTATIC*/0, /*CHARMED*/0, /*SPARKLE*/0,
  /*WINK*/0, /*STARRY*/900, /*LAUGH*/0, /*EAGER*/0, /*ANTICIPATION*/0,
  /*POUT*/0, /*TEARY*/1800, /*CRYING*/1300, /*DISAPPOINTED*/0, /*HEARTBROKEN*/0,
  /*GRUMPY*/0, /*FURIOUS*/0, /*FUMING*/0, /*ANNOYED*/0, /*FLUSHED*/0,
  /*QUESTIONING*/0, /*INVESTIGATING*/900, /*THOUGHTFUL*/0, /*CONFUSED*/400, /*WONDER*/0,
  /*PLAYFUL*/0, /*MISCHIEVOUS*/0, /*CHEEKY*/0, /*DERP*/0, /*LOVING*/0,
  /*TIRED*/0, /*SLEEPING*/2600, /*YAWNING*/0, /*SLEEPY*/0, /*SCARED*/0,
  /*BORED*/0, /*EMBARRASSED*/0, /*HUNGRY*/1500,
};

static QueueHandle_t sndQueue;

static int16_t lastSample = 0;   // for the cut-off fade (see cutFade)

static void i2sWriteSamples(int16_t* mono, size_t n) {
  static int16_t stereo[CHUNK * 2];
  for (size_t i = 0; i < n; i++) { stereo[2*i] = mono[i]; stereo[2*i+1] = mono[i]; }
  if (n) lastSample = mono[n - 1];
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  // write() can return short — dropped samples crackle. Push until done.
  uint8_t* p = (uint8_t*)stereo;
  size_t left = n * 4;
  while (left) {
    size_t w = i2s.write(p, left);
    p += w; left -= w;
    if (!w) vTaskDelay(1);
  }
#else
  size_t written;
  i2s_write(I2S_NUM_0, stereo, n * 4, &written, portMAX_DELAY);
#endif
}

// A sound cut off for a newer one stops at whatever amplitude it happened to be
// at. That step to the next waveform is a hard discontinuity — the audible
// click users hear as "the sound breaking". Ramp the last value down to zero
// over ~8 ms instead; short enough that the new sound still feels instant.
static void cutFade() {
  static int16_t ramp[128];
  int16_t from = lastSample;
  if (!from) return;
  for (uint8_t i = 0; i < 128; i++) ramp[i] = (int16_t)((int32_t)from * (127 - i) / 127);
  i2sWriteSamples(ramp, 128);
  lastSample = 0;
}

// Renders one sound. Returns early (true) if a new sound arrived in the queue.
static bool renderSound(const SoundDef& s) {
  static int16_t buf[CHUNK];
  float phase = 0;  // continuous across segments -> no clicks
  for (uint8_t si = 0; si < s.n; si++) {
    Seg seg;
    memcpy(&seg, &s.segs[si], sizeof(Seg));
    uint32_t total = (uint32_t)seg.ms * SAMPLE_RATE / 1000;
    uint32_t fadeN = SAMPLE_RATE * 25 / 1000;          // 25 ms rounded edges
    if (fadeN > total / 2) fadeN = total / 2;
    uint32_t done = 0;
    while (done < total) {
      size_t n = min((uint32_t)CHUNK, total - done);
      for (size_t i = 0; i < n; i++) {
        uint32_t pos = done + i;
        float t = (float)pos / total;
        float f = seg.f0 + (seg.f1 - (float)seg.f0) * t;
        if (seg.vibDep) f += seg.vibDep * sinf(TWO_PI * seg.vibHz * pos / SAMPLE_RATE);
        phase += TWO_PI * f / SAMPLE_RATE;
        if (phase > TWO_PI) phase -= TWO_PI;
        float env = 1.0f;  // raised-cosine attack/release: soft, no harsh edges
        if (pos < fadeN)              env = 0.5f - 0.5f * cosf(PI * pos / fadeN);
        else if (total - pos < fadeN) env = 0.5f - 0.5f * cosf(PI * (total - pos) / fadeN);
        float w;
        if (seg.wave == 2)      w = random(-1000, 1001) / 1000.0f * 0.6f;      // static
        else if (seg.wave == 1) w = 0.6f * tanhf(3.5f * sinf(phase));          // rounded square:
                                                                               // buzzy but no aliasing crackle
        else                    w = sinf(phase);
        buf[i] = (int16_t)(w * env * seg.amp * SND_GAIN);
      }
      i2sWriteSamples(buf, n);
      done += n;
      if (uxQueueMessagesWaiting(sndQueue) > 0) { cutFade(); return true; }  // cut for new sound
    }
  }
  return false;
}

// Streams a flash PCM clip. Returns early (true) if a new sound is queued.
static bool renderPcm(const Clip& c) {
  static int16_t buf[CHUNK];
  uint32_t pos = 0;
  while (pos < c.len) {
    size_t n = min((uint32_t)CHUNK, c.len - pos);
    for (size_t i = 0; i < n; i++) buf[i] = (int16_t)(c.pcm[pos + i] * CLIP_VOLUME);
    i2sWriteSamples(buf, n);
    pos += n;
    if (uxQueueMessagesWaiting(sndQueue) > 0) { cutFade(); return true; }
  }
  return false;
}

// fill the DMA pipeline with true silence so nothing loops/crackles after a sound
static void flushSilence() {
  static int16_t zeros[CHUNK] = {0};
  for (uint8_t i = 0; i < 5; i++) i2sWriteSamples(zeros, CHUNK);  // covers all DMA buffers
}

static bool playId(uint8_t id) {   // returns true if cut short by a new sound
  if (id == BOOT_ID) return renderSound(SOUND_BOOT);
  if (id < FACE_COUNT) return renderSound(SOUNDS[id]);
  if (id >= CLIP_ID_BASE && id < CLIP_ID_BASE + CLIP_COUNT)
    return renderPcm(CLIPS[id - CLIP_ID_BASE]);
  return false;
}

static void soundTask(void*) {
  uint8_t id;
  for (;;) {
    if (xQueueReceive(sndQueue, &id, portMAX_DELAY) != pdTRUE) continue;
    for (;;) {                        // play once, then loop if the face is continuous
      bool cut = playId(id);
      if (cut) break;                 // new sound queued -> outer loop picks it up
      flushSilence();
      uint16_t gap = id < FACE_COUNT ? LOOP_GAP_MS[id] : 0;
      if (!gap) break;
      uint8_t nid;                    // wait out the gap, unless a new sound arrives
      if (xQueueReceive(sndQueue, &nid, pdMS_TO_TICKS(gap)) == pdTRUE) id = nid;
    }
  }
}

void soundBegin() {
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
  cfg.dma_buf_count = 6;           // extra slack against underruns
  cfg.dma_buf_len = CHUNK;
  cfg.tx_desc_auto_clear = true;   // output zeros on underrun, never loop old audio
  i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
  i2s_pin_config_t pins = {};
  pins.bck_io_num = PIN_I2S_BCLK;
  pins.ws_io_num = PIN_I2S_LRC;
  pins.data_out_num = PIN_I2S_DIN;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  i2s_set_pin(I2S_NUM_0, &pins);
#endif
  sndQueue = xQueueCreate(2, sizeof(uint8_t));
  // ponytail: audio on core 0, everything else (render/logic) stays on core 1
  xTaskCreatePinnedToCore(soundTask, "snd", 4096, nullptr, 3, nullptr, 0);  // outrank housekeeping tasks
}

static void enqueue(uint8_t id) {
  if (!sndQueue) return;
  xQueueReset(sndQueue);           // newest emotion wins
  xQueueSend(sndQueue, &id, 0);
}

void soundPlayFace(FaceExpr f) {
  if (f >= FACE_COUNT) return;
  int8_t c = CLIP_FOR_FACE[f];
  if (c >= 0 && random(2)) {        // cherry on top: mapped faces use their clip ~50%
    Serial.printf("snd: clip %d for face %d\n", c, f);
    enqueue(CLIP_ID_BASE + c);
  } else {
    enqueue((uint8_t)f);
  }
}
void soundPlayClip(ClipId c) { if (c < CLIP_COUNT) enqueue(CLIP_ID_BASE + (uint8_t)c); }
void soundPlayBoot()         { enqueue(CLIP_ID_BASE + CLIP_HELLO); }   // greeting clip
