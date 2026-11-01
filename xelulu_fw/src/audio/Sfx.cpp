// Port of Eillik's EilikSound.cpp. Same segment synthesis (continuous phase,
// 25 ms raised-cosine edges, vibrato, soft square, noise), same clip map and
// 50 % randomiser, same 8 ms cutFade and newest-wins. What changed (§10.2):
// it renders on demand inside the audio task (pull, not push), and ambient
// loops are off except the sleeping snore, which the caller asks for.
#include "Sfx.h"
#include "../store/Store.h"
#include "ClipsData.h"   // flash PCM arrays (only this file may include it)

#define SAMPLE_RATE 16000

// One sound = a list of segments. amp 0 = silent gap.
// wave: 0 sine (soft), 1 soft square (buzzy), 2 noise (static).
// vibHz/vibDep: optional vibrato (shimmer/wobble/boing effects).
struct Seg {
  uint16_t f0, f1, ms;
  uint8_t  amp, wave, vibHz, vibDep;
};

// ---- 38 effects, indexed by FaceId (Eilik_Sound_Generation_Prompt.md) ----
// Row 1: Joy & Excitement — major, ascending, fast bounce
static const Seg S_SMILE_[]     = {{800,1500,180,150}};                                                     // quick upward chirp
static const Seg S_GIDDY_[]     = {{1300,1400,70,150},{0,0,30,0},{1600,1700,70,150},{0,0,30,0},{1450,1350,70,145},{0,0,30,0},{1750,1850,90,150}};
static const Seg S_ECSTATIC_[]  = {{1046,1046,60,140},{1175,1175,60,142},{1318,1318,60,144},{1480,1480,60,146},{1568,1568,60,148},{0,0,20,0},{1900,2100,70,160}};
static const Seg S_CHARMED_[]   = {{880,880,220,130},{0,0,40,0},{1175,1175,320,135}};                       // two-tone chime
static const Seg S_SPARKLE_[]   = {{1568,1568,70,140},{1976,1976,70,140},{2349,2349,70,140},{0,0,40,0},{2093,2637,110,135}};
static const Seg S_WINK_[]      = {{600,900,110,150},{0,0,30,0},{1400,1500,80,150},{0,0,20,0},{1900,2300,50,160}};
static const Seg S_STARRY_[]    = {{1300,1300,700,120,0,9,60}};                                             // shimmering vibrato
static const Seg S_LAUGH_[]     = {{1000,860,90,150},{0,0,60,0},{980,840,90,145},{0,0,60,0},{950,820,90,140},{0,0,60,0},{920,800,110,135}};
static const Seg S_EAGER_[]     = {{900,1300,110,145},{0,0,50,0},{1100,1600,130,150}};
static const Seg S_ANTICIP_[]   = {{700,750,90,120},{0,0,160,0},{750,800,90,125},{0,0,120,0},{800,860,80,130},{0,0,80,0},{860,930,80,140},{0,0,50,0},{930,1050,70,150}};
// Row 2: Sadness & Anger — descending, dissonant/buzzy
static const Seg S_POUT_[]      = {{700,520,300,120},{520,380,400,95}};                                     // digital sigh
static const Seg S_TEARY_[]     = {{750,500,900,110,0,6,35}};                                               // wobbly descent
static const Seg S_CRYING_[]    = {{1200,900,140,120},{0,0,350,0},{1100,800,150,110},{0,0,400,0},{1000,700,170,100}};
static const Seg S_DISAPP_[]    = {{360,310,140,140},{310,255,220,130}};                                    // "womp"
static const Seg S_HEARTBRK_[]  = {{1500,1500,180,140},{0,0,40,0},{380,300,350,140,1},{300,240,250,110,1}}; // bleep cracks to buzz
static const Seg S_GRUMPY_[]    = {{320,305,100,150,1},{0,0,70,0},{300,285,100,150,1},{0,0,70,0},{280,265,140,150,1}};
static const Seg S_FURIOUS_[]   = {{900,700,80,160,1},{0,0,30,0},{1200,1400,70,160,1},{0,0,25,0},{600,500,90,160,1},{0,0,30,0},{1500,1100,110,165,1}};
static const Seg S_FUMING_[]    = {{300,300,50,140,1},{0,0,220,0},{300,300,50,140,1},{0,0,180,0},{300,300,50,145,1},{0,0,140,0},{300,300,50,150,1},{0,0,100,0},{0,0,180,100,2}};  // ticking -> static
static const Seg S_ANNOYED_[]   = {{420,420,220,140}};                                                      // flat monotone
static const Seg S_FLUSHED_[]   = {{800,900,60,130,1},{0,0,25,0},{1000,1150,55,135,1},{0,0,20,0},{1250,1450,55,140,1},{0,0,15,0},{1550,1800,60,150,1},{1900,2200,70,150,1}};
// Row 3: Curiosity, Playful & Affection — pitch bends, vocal inflections
static const Seg S_QUESTION_[]  = {{600,650,160,120},{650,1200,180,145}};                                   // "hm?"
static const Seg S_INVESTIG_[]  = {{1300,1250,120,130},{0,0,330,0},{1300,1250,120,125},{0,0,330,0},{1300,1250,120,120}};  // sonar pings
static const Seg S_THOUGHT_[]   = {{700,700,160,120},{0,0,120,0},{950,950,160,120},{0,0,120,0},{700,700,160,115},{0,0,120,0},{950,950,160,115}};
static const Seg S_CONFUSED_[]  = {{500,900,260,125},{900,500,260,125},{500,900,260,120},{900,450,300,110}};
static const Seg S_WONDER_[]    = {{350,1300,450,130},{1300,1280,350,110}};                                 // slide up, hold softly
static const Seg S_PLAYFUL_[]   = {{1100,1150,80,150},{0,0,40,0},{1300,1350,60,120},{0,0,90,0},{950,1000,70,140},{0,0,30,0},{1450,1500,90,155}};
static const Seg S_MISCHIEF_[]  = {{600,620,90,60},{0,0,40,0},{750,780,90,85},{0,0,40,0},{900,940,90,110},{0,0,40,0},{1100,1150,90,140},{0,0,30,0},{1600,1900,80,160}};  // quiet -> loud
static const Seg S_CHEEKY_[]    = {{1500,600,220,150,0,18,120},{550,480,120,110,0,12,60}};                  // springy boing
static const Seg S_DERP_[]      = {{820,790,120,130},{0,0,90,0},{1237,1219,90,135},{0,0,150,0},{600,640,140,125},{0,0,60,0},{1490,1400,110,130}};
static const Seg S_LOVING_[]    = {{600,600,110,140},{0,0,60,0},{450,450,140,130},{0,0,420,0},{600,600,110,135},{0,0,60,0},{450,450,140,125}};  // ba-bum ba-bum
// Row 4: Fatigue & Reactive — low energy or sudden bursts
static const Seg S_TIRED_[]     = {{600,420,350,120},{420,300,450,80}};
static const Seg S_SLEEPING_[]  = {{265,250,500,90},{0,0,350,0},{250,235,500,70}};                          // fading hums / snore
static const Seg S_YAWNING_[]   = {{500,780,400,110,0,5,30},{780,300,700,90,0,5,35}};
static const Seg S_SLEEPY_[]    = {{440,430,250,110},{0,0,180,0},{415,400,300,100},{0,0,220,0},{380,360,380,85}};
static const Seg S_SCARED_[]    = {{1800,1850,45,130},{0,0,35,0},{1750,1800,45,130},{0,0,35,0},{1850,1900,45,130},{0,0,35,0},{1700,1750,45,125},{0,0,35,0},{1800,1850,60,120}};
static const Seg S_BORED_[]     = {{500,490,260,120},{490,380,180,90}};                                     // unenthusiastic "meh"
static const Seg S_EMBARR_[]    = {{900,1000,60,130},{1050,1150,60,135},{1200,1300,60,140},{1350,1500,60,145},{1550,1750,60,150}};  // trill, cut
static const Seg S_HUNGRY_[]    = {{1400,1400,110,140},{0,0,50,0},{950,950,110,135},{0,0,120,0},{1400,1400,110,140},{0,0,50,0},{950,950,110,135}};
// Xelulu additions (§10.2)
static const Seg S_PURR_[]      = {{200,200,1200,100,0,5,20}};                                              // 180-220 Hz soft vibrato
static const Seg S_CLAP_[]      = {{0,0,30,150,2}};                                                         // 30 ms noise burst
static const Seg S_LISTEN_[]    = {{600,1200,120,140}};                                                     // earcon
static const Seg S_EDGE_[]      = {{1400,500,260,150,0,6,40}};                                              // quick falling "whoa"
static const Seg S_POP_[]       = {{1800,2400,60,140},{0,0,20,0},{2400,3000,50,120}};                       // head-tap sparkle
static const Seg S_BREATHE_[]   = {{0,0,500,25,2},{0,0,600,45,2},{0,0,500,25,2}};                           // soft swell
static const Seg S_VOL_[]       = {{1000,1000,90,140}};                                                     // volume confirm
static const Seg S_CHARGE_[]    = {{523,523,120,140},{659,659,120,140},{784,1046,200,150}};                  // kept for later

static const Seg S_BOOT_[]      = {{392,392,150,150},{0,0,50,0},{523,523,150,150},{0,0,50,0},{659,784,240,160}};

struct SoundDef { const Seg* segs; uint8_t n; };
#define SND(s) {s, sizeof(s)/sizeof(Seg)}
static const SoundDef SOUNDS[SND_COUNT + 1] = {
  SND(S_SMILE_), SND(S_GIDDY_), SND(S_ECSTATIC_), SND(S_CHARMED_), SND(S_SPARKLE_),
  SND(S_WINK_), SND(S_STARRY_), SND(S_LAUGH_), SND(S_EAGER_), SND(S_ANTICIP_),
  SND(S_POUT_), SND(S_TEARY_), SND(S_CRYING_), SND(S_DISAPP_), SND(S_HEARTBRK_),
  SND(S_GRUMPY_), SND(S_FURIOUS_), SND(S_FUMING_), SND(S_ANNOYED_), SND(S_FLUSHED_),
  SND(S_QUESTION_), SND(S_INVESTIG_), SND(S_THOUGHT_), SND(S_CONFUSED_), SND(S_WONDER_),
  SND(S_PLAYFUL_), SND(S_MISCHIEF_), SND(S_CHEEKY_), SND(S_DERP_), SND(S_LOVING_),
  SND(S_TIRED_), SND(S_SLEEPING_), SND(S_YAWNING_), SND(S_SLEEPY_), SND(S_SCARED_),
  SND(S_BORED_), SND(S_EMBARR_), SND(S_HUNGRY_),
  SND(S_PURR_), SND(S_CLAP_), SND(S_LISTEN_), SND(S_EDGE_), SND(S_POP_),
  SND(S_BREATHE_), SND(S_VOL_), SND(S_CHARGE_),
  SND(S_BOOT_),
};

const char* const SOUND_NAMES[SND_COUNT + 1] = {
  "S_SMILE", "S_GIDDY", "S_ECSTATIC", "S_CHARMED", "S_SPARKLE", "S_WINK", "S_STARRY", "S_LAUGH",
  "S_EAGER", "S_ANTICIP", "S_POUT", "S_TEARY", "S_CRYING", "S_DISAPP", "S_HEARTBRK", "S_GRUMPY",
  "S_FURIOUS", "S_FUMING", "S_ANNOYED", "S_FLUSHED", "S_QUESTION", "S_INVESTIG", "S_THOUGHT",
  "S_CONFUSED", "S_WONDER", "S_PLAYFUL", "S_MISCHIEF", "S_CHEEKY", "S_DERP", "S_LOVING", "S_TIRED",
  "S_SLEEPING", "S_YAWNING", "S_SLEEPY", "S_SCARED", "S_BORED", "S_EMBARR", "S_HUNGRY",
  "S_PURR", "S_CLAP", "S_LISTEN", "S_EDGE", "S_POP", "S_BREATHE", "S_VOL", "S_CHARGE", "S_BOOT",
};

// ---- WAV clips (flash PCM) + face mapping ----
#define CLIP_VOLUME 0.9f
struct Clip { const int16_t* pcm; uint32_t len; };
static const Clip CLIPS[CLIP_COUNT] = {
  {PCM_BAHHH, PCM_BAHHH_LEN}, {PCM_HEHE, PCM_HEHE_LEN}, {PCM_HELLO, PCM_HELLO_LEN},
  {PCM_OI, PCM_OI_LEN}, {PCM_PIGYAWN, PCM_PIGYAWN_LEN}, {PCM_RISA, PCM_RISA_LEN},
  {PCM_SOARYN, PCM_SOARYN_LEN}, {PCM_SUS, PCM_SUS_LEN}, {PCM_WHAT, PCM_WHAT_LEN},
  {PCM_ARGHH, PCM_ARGHH_LEN},
};
const char* const CLIP_NAMES[CLIP_COUNT] = {
  "bahhh", "hehe", "hello", "oi", "pigyawn", "risa", "soaryn", "sus", "what", "arghh",
};
// which faces have a clip voice (-1 = synth only), indexed by FaceId (Eillik's QA-approved map)
static const int8_t CLIP_FOR_FACE[FACE_COUNT] = {
  /*SMILE*/-1, /*GIDDY*/CLIP_RISA, /*ECSTATIC*/-1, /*CHARMED*/-1, /*SPARKLE*/-1,
  /*WINK*/-1, /*STARRY*/CLIP_SOARYN, /*LAUGH*/CLIP_RISA, /*EAGER*/-1, /*ANTICIPATION*/-1,
  /*POUT*/-1, /*TEARY*/-1, /*CRYING*/-1, /*DISAPPOINTED*/-1, /*HEARTBROKEN*/-1,
  /*GRUMPY*/CLIP_BAHHH, /*FURIOUS*/CLIP_ARGHH, /*FUMING*/-1, /*ANNOYED*/CLIP_BAHHH, /*FLUSHED*/-1,
  /*QUESTIONING*/CLIP_SUS, /*INVESTIGATING*/CLIP_SUS, /*THOUGHTFUL*/-1, /*CONFUSED*/CLIP_WHAT, /*WONDER*/CLIP_OI,
  /*PLAYFUL*/CLIP_HEHE, /*MISCHIEVOUS*/CLIP_HEHE, /*CHEEKY*/CLIP_BAHHH, /*DERP*/CLIP_OI, /*LOVING*/-1,
  /*TIRED*/-1, /*SLEEPING*/-1, /*YAWNING*/CLIP_PIGYAWN, /*SLEEPY*/-1, /*SCARED*/-1,
  /*BORED*/-1, /*EMBARRASSED*/-1, /*HUNGRY*/-1,
  /*LISTENING..UPDATING*/-1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

// ---------------------------------------------------------------------------
// Voice state (audio task only). Requests arrive through a 1-deep "mailbox":
// the newest request overwrites any older one that has not started yet.
// ---------------------------------------------------------------------------
struct Req { uint8_t kind, id, atten; uint16_t gap; };   // kind 0 synth, 1 clip, 2 stop
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool pending = false;
static Req req;

static const SoundDef* snd = nullptr;   // active synth sound
static const Clip* clip = nullptr;      // active clip
static uint8_t curId = 0, si = 0;
static uint32_t pos = 0, total = 0, fadeN = 0, gapLeft = 0;
static float phase = 0, gain = 1;
static uint16_t loopGap = 0;
static int16_t lastSample = 0;
static uint16_t fadeLeft = 0;
static float fadeFrom = 0;
static Req next;
static bool haveNext = false;
static volatile bool busy = false;

static void post(uint8_t kind, uint8_t id, uint8_t atten, uint16_t gap) {
  portENTER_CRITICAL(&mux);
  req = {kind, id, atten, gap};
  pending = true;
  busy = kind != 2;
  portEXIT_CRITICAL(&mux);
}

void sfxPlay(uint8_t id, uint8_t attenDb, uint16_t loopGapMs) {
  if (id > SND_COUNT) return;
  post(0, id, attenDb, loopGapMs);
}
void sfxPlayClip(ClipId c) { if (c < CLIP_COUNT) post(1, c, 0, 0); }
void sfxStop() { post(2, 0, 0, 0); }
bool sfxBusy() { return busy; }

void sfxPlayFace(FaceId f) {
  if (f >= FACE_COUNT) return;
  int8_t c = CLIP_FOR_FACE[f];
  if (c >= 0 && random(2)) {        // cherry on top: mapped faces use their clip ~50%
    Serial.printf("snd: clip %s for face %d\n", CLIP_NAMES[c], f + 1);
    sfxPlayClip((ClipId)c);
  } else if (f < 38) {
    sfxPlay(f);
  } else if (f == FACE_LISTENING) {
    sfxPlay(S_LISTEN);
  } else if (f == FACE_BREATHE) {
    sfxPlay(S_BREATHE);
  }
}

static void loadSeg() {
  const Seg& s = snd->segs[si];
  total = (uint32_t)s.ms * SAMPLE_RATE / 1000;
  fadeN = SAMPLE_RATE * 25 / 1000;          // 25 ms rounded edges
  if (fadeN > total / 2) fadeN = total / 2;
  pos = 0;
}

static void start(const Req& r) {
  snd = nullptr; clip = nullptr; gapLeft = 0;
  if (r.kind == 2) { busy = false; return; }
  gain = powf(10.0f, -r.atten / 20.0f);
  loopGap = r.gap;
  phase = 0;                                // continuous across segments -> no clicks
  if (r.kind == 1) { clip = &CLIPS[r.id]; pos = 0; }
  else { curId = r.id; snd = &SOUNDS[r.id]; si = 0; loadSeg(); }
}

static int16_t synthSample() {
  Seg seg;
  memcpy(&seg, &snd->segs[si], sizeof(Seg));
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
  if (++pos >= total) {
    if (++si < snd->n) loadSeg();
    else snd = nullptr;                     // done: caller handles the loop gap
  }
  return (int16_t)(w * env * seg.amp * cal.sndGain * gain);
}

void sfxRender(int16_t* out, int n) {
  if (pending) {
    portENTER_CRITICAL(&mux);
    next = req; pending = false;
    portEXIT_CRITICAL(&mux);
    haveNext = true;
    // A sound cut off for a newer one stops at whatever amplitude it was at;
    // ramp that last value to zero over 128 samples (8 ms) instead of clicking.
    if (lastSample && (snd || clip)) { fadeFrom = lastSample; fadeLeft = 128; }
    snd = nullptr; clip = nullptr; gapLeft = 0;
  }
  for (int i = 0; i < n; i++) {
    int16_t s = 0;
    if (fadeLeft) {
      s = (int16_t)(fadeFrom * --fadeLeft / 128);
    } else {
      if (haveNext) { haveNext = false; start(next); }
      if (snd) {
        s = synthSample();
        if (!snd && loopGap) gapLeft = (uint32_t)loopGap * (SAMPLE_RATE / 1000);   // wait out the gap
      } else if (clip) {
        s = (int16_t)(clip->pcm[pos] * CLIP_VOLUME * gain);
        if (++pos >= clip->len) clip = nullptr;
      } else if (gapLeft && --gapLeft == 0) {
        snd = &SOUNDS[curId]; si = 0; loadSeg();                // the snore, again
      }
    }
    out[i] = s;
    lastSample = s;
  }
  if (!snd && !clip && !fadeLeft && !haveNext && !gapLeft) busy = false;
}
