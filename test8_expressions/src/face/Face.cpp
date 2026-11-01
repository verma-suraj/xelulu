// Port of Eillik's EilikFace.cpp (FW-SOT §9.1). The 38 faces are drawn by the
// same code on the same 240x240 canvas; what changed:
//  - every mouth goes through MOUTH(...): the speaking overlay replaces it,
//    and the mouth follows only 35 % of the eye gaze
//  - a global eye gaze (gzx, gzy) and eye-height tilt stand in for head tilt
//  - FURIOUS flames drift smoothly instead of jumping every frame (photosensitivity)
//  - faces 39-48 are new, drawn with the same primitives
//  - the sprite is pushed through a 256-entry palette LUT: that LUT is the
//    pixel dimming (the backlight is hard-wired to 3.3 V), and it feeds DMA
#include "Face.h"
#include "Glow.h"
#include "../../Pins.h"
#include "../../Config.h"
#include "../store/Store.h"
#include "../State.h"
#include <TFT_eSPI.h>

static_assert(TFT_CS == PIN_TFT_CS && TFT_DC == PIN_TFT_DC && TFT_RST == PIN_TFT_RST &&
              TFT_MOSI == PIN_SPI_MOSI && TFT_SCLK == PIN_SPI_SCLK && TFT_MISO == PIN_SPI_MISO,
              "build_opt.h display pins must match Pins.h");

#define MID 120
#define EYE_L 74
#define EYE_R 166
#define EYE_Y 105
#define K TFT_BLACK

// palette matched to Animation sheet.png (565; the 8-bit sprite quantizes)
#define C_CYAN   0x07FF   // ice blue main
#define C_DIM    0x03EF   // half-bright cyan
#define C_SKY    0x867D   // watery light blue (tears)
#define C_PINK   0xFE19   // mouths, hearts, tongue
#define C_ROSE   0xB98D   // blush / inner tongue / dim pink
#define C_RED    0xF800
#define C_DRED   0x9000
#define C_ORANGE 0xFDA0   // grumpy/annoyed amber
#define C_YELLOW 0xFFE0   // starry eyes gold
#define C_CREAM  0xFF96   // yawning pale warm
#define C_PEACH  0xFDAF   // smirk mouths, drool
#define C_TEAL   0x9737   // hungry fish pale green
#define C_VIOLET 0xCD1F   // confused spiral pastel
#define C_WHITE  0xFFFF
#define C_GREY   0x7BEF
// Xelulu additions
#define C_AMBER  0xFD88   // #FFB347 story / comfort amber
#define C_WCYAN  0x7F7C   // warm cyan (storyteller)
#define C_GREEN  0x2F2D   // updating

const char* const FACE_NAMES[FACE_COUNT] = {
  "SMILE", "GIDDY", "ECSTATIC", "CHARMED", "SPARKLING JOY", "PLAYFUL WINK", "STARRY EYES",
  "CHEERFUL LAUGH", "EAGER", "ANTICIPATION", "POUT", "TEARY EYES", "CRYING SOFTLY",
  "DISAPPOINTED", "HEARTBROKEN", "GRUMPY", "FURIOUS", "FUMING", "ANNOYED", "FLUSHED",
  "QUESTIONING", "INVESTIGATING", "THOUGHTFUL", "CONFUSED SPIRAL", "WONDER", "PLAYFUL",
  "MISCHIEVOUS", "CHEEKY TONGUE", "SILLY DERP", "LOVING", "TIRED", "SLEEPING ZZZ",
  "YAWNING", "SLEEPY", "SCARED", "BORED MEH", "EMBARRASSED", "HUNGRY",
  "LISTENING", "THINKING", "COMFORT", "BREATHE", "STORYTELLER", "CHARGING", "LOW BATTERY",
  "UPSIDE DOWN", "OFFLINE SORRY", "UPDATING",
};

static TFT_eSPI tft;
static TFT_eSprite* spr = nullptr;
static FaceId cur = FACE_SMILE;
static unsigned long setT = 0;

// per-frame globals: breathing offset, blink scale, squash widen
static float ox = 0, oy = 0;
static float bs = 1.0f;
static float rxs = 1.0f;

// gaze / tilt / speech, set from other tasks
static volatile int8_t gazeTx = 0, gazeTy = 0, tiltTarget = 0, listenSide = 0;
static volatile bool speaking = false, squashReq = false, blinkReq = false;
static volatile float envelope = 0;
static float gzx = 0, gzy = 0, tiltPx = 0;
static bool inMouth = false, mouthDrawn = false;
static float saveOx, saveOy;

// eyes tilt for the curious look: left eye up, right eye down (or the reverse)
static inline float tiltAt(float x) { return inMouth ? 0 : (x < MID ? -0.5f : 0.5f) * tiltPx; }

// ---------- primitives (all apply ox/oy) ----------
static void ell(float x, float y, float rx, float ry, uint16_t c) {
  if (ry < 2) ry = 2;
  spr->fillEllipse((int)(x + ox), (int)(y + oy + tiltAt(x)), (int)(rx * rxs), (int)ry, c);
}
static void rrc(float x, float y, float w, float h, float r, uint16_t c) {
  spr->fillRoundRect((int)(x + ox), (int)(y + oy + tiltAt(x + w / 2)), (int)w, (int)h, (int)r, c);
}
static void wl(float x0, float y0, float x1, float y1, float w, uint16_t c) {
  float ty = tiltAt((x0 + x1) / 2);
  spr->drawWideLine(x0 + ox, y0 + oy + ty, x1 + ox, y1 + oy + ty, w, c, K);
}
static void circ(float x, float y, float r, uint16_t c) {
  spr->fillCircle((int)(x + ox), (int)(y + oy + tiltAt(x)), (int)r, c);
}
// n-shaped arc (happy eye / frown mouth)
static void arcUp(float x, float y, float r, float t, uint16_t c) {
  int X = x + ox, Y = y + oy + tiltAt(x);
  spr->fillCircle(X, Y, (int)r, c);
  spr->fillCircle(X, Y, (int)(r - t), K);
  spr->fillRect(X - r - 1, Y, 2 * r + 3, r + 2, K);
}
// u-shaped arc (smile mouth)
static void arcDn(float x, float y, float r, float t, uint16_t c) {
  int X = x + ox, Y = y + oy + tiltAt(x);
  spr->fillCircle(X, Y, (int)r, c);
  spr->fillCircle(X, Y, (int)(r - t), K);
  spr->fillRect(X - r - 1, Y - r - 1, 2 * r + 3, r + 2, K);
}
static void heart(float x, float y, float s, uint16_t c) {
  y += tiltAt(x);
  spr->fillCircle((int)(x + ox - 0.32f * s), (int)(y + oy - 0.22f * s), (int)(0.36f * s), c);
  spr->fillCircle((int)(x + ox + 0.32f * s), (int)(y + oy - 0.22f * s), (int)(0.36f * s), c);
  spr->fillTriangle(x + ox - 0.64f * s, y + oy - 0.02f * s,
                    x + ox + 0.64f * s, y + oy - 0.02f * s,
                    x + ox, y + oy + 0.75f * s, c);
}
static void star4(float x, float y, float r, float rot, float und, float ph, uint16_t c) {
  y += tiltAt(x);
  for (uint8_t k = 0; k < 4; k++) {
    float a = rot + k * HALF_PI;
    float rk = r * (1.0f + und * sinf(ph + k * 1.7f));
    float w = r * 0.34f;
    spr->fillTriangle(x + ox + rk * cosf(a),          y + oy + rk * sinf(a),
                      x + ox + w * cosf(a + HALF_PI), y + oy + w * sinf(a + HALF_PI),
                      x + ox + w * cosf(a - HALF_PI), y + oy + w * sinf(a - HALF_PI), c);
  }
  spr->fillCircle((int)(x + ox), (int)(y + oy), (int)(r * 0.34f), c);
}
// open smiling mouth: pink with lighter tongue inside (sheet style)
static void mouthOpen(float x, float y, float rx, float ry) {
  ell(x, y, rx, ry, C_PINK);
  ell(x, y + ry * 0.35f, rx * 0.6f, ry * 0.45f, C_ROSE);
}
// soft cheek blush under the eyes (sheet style)
static void blush(uint16_t c = C_ROSE) {
  ell(EYE_L - 6, 138, 15, 6, c);
  ell(EYE_R + 6, 138, 15, 6, c);
}
static float bump(float p, float c, float w) {
  float d = fabsf(p - c);
  return d > w ? 0 : 0.5f + 0.5f * cosf(PI * d / w);
}
// small glyphs for the Xelulu faces
static void battery(float x, float y, uint16_t bar) {
  rrc(x - 18, y - 9, 36, 18, 4, C_GREY);
  rrc(x - 15, y - 6, 30, 12, 2, K);
  rrc(x + 18, y - 4, 4, 8, 1, C_GREY);
  if (bar) rrc(x - 13, y - 4, 7, 8, 1, bar);
}

// ---------- mouth gate (§9.1): one helper for every face's mouth ----------
// While SPEAKING the overlay mouth replaces the face's own. The mouth keeps
// only 35 % of the gaze, so the eyes look around and the face stays put.
static bool mouthBegin() {
  mouthDrawn = true;
  saveOx = ox; saveOy = oy;
  ox -= 0.65f * gzx; oy -= 0.65f * gzy;
  inMouth = true;
  if (speaking) {
    float e = envelope;
    mouthOpen(MID, 156, 9 + 7 * e, 2 + 13 * e);
    return false;
  }
  return true;
}
static void mouthEnd() { ox = saveOx; oy = saveOy; inMouth = false; }
#define MOUTH(...) do { if (mouthBegin()) { __VA_ARGS__; } mouthEnd(); } while (0)

// ---------- organic blink engine (global rule) ----------
static uint8_t blinkPhase = 0;
static unsigned long blinkT = 0, blinkNext = 3000;

static void blinkEngine(unsigned long now) {
  switch (blinkPhase) {
    case 0:
      bs = 1.0f;
      if (now > blinkNext) { blinkPhase = 1; blinkT = now; }
      break;
    case 1: {
      float e = (now - blinkT) / 80.0f;
      bs = 1.0f - 0.9f * min(e, 1.0f);
      if (e >= 1) { blinkPhase = 2; blinkT = now; }
      break;
    }
    case 2: {
      float e = (now - blinkT) / 150.0f;
      bs = 0.1f + 1.0f * min(e, 1.0f);
      if (e >= 1) { blinkPhase = 3; blinkT = now; }
      break;
    }
    case 3: {
      float e = (now - blinkT) / 120.0f;
      bs = 1.1f - 0.1f * min(e, 1.0f);
      if (e >= 1) { blinkPhase = 0; blinkNext = now + random(3000, 7000); }
      break;
    }
  }
  rxs = 1.0f + (1.0f - min(bs, 1.0f)) * 0.18f;
}

// ---------- the 48 faces ----------
static void draw(unsigned long now) {
  float t = (now - setT) / 1000.0f;
  float b = bs;

  switch (cur) {

    // ================= Row 1: Joy & Excitement =================
    case FACE_SMILE: {  // rounded-square ice-blue eyes + cyan smile
      float ph = now % 4000;
      float ws = ph < 1000 ? 1.0f + 0.12f * sinf(PI * ph / 1000) : 1.0f;
      float gl = 1.0f + 0.04f * sinf(now * 0.004f);   // soft throb
      float h = 32 * b * gl;
      rrc(EYE_L - 14 * ws, EYE_Y - 10 - h / 2, 28 * ws, h, 9, C_CYAN);
      rrc(EYE_R - 14 * ws, EYE_Y - 10 - h / 2, 28 * ws, h, 9, C_CYAN);
      MOUTH(arcDn(MID, 148, 13, 5, C_CYAN));
      break;
    }
    case FACE_GIDDY: {  // cyan arcs + open pink mouth + blush, tandem bounce
      float bp = fabsf(sinf(now * 0.012f));
      oy -= 7 * bp;
      float sq = bp > 0.85f ? 1.15f : 1.0f;
      arcUp(EYE_L, 102, 23 * sq, 9, C_CYAN);
      arcUp(EYE_R, 102, 23 * sq, 9, C_CYAN);
      blush();
      MOUTH(mouthOpen(MID, 152, 13, 10));
      break;
    }
    case FACE_ECSTATIC: {  // heartbeat pulse + particles, bigger mouth
      float s = 1.0f + 0.08f * sinf(now * 0.008f);
      arcUp(EYE_L, 102, 24 * s, 11 * s, C_CYAN);
      arcUp(EYE_R, 102, 24 * s, 11 * s, C_CYAN);
      blush();
      MOUTH(mouthOpen(MID, 154, 17 * s, 13 * s));
      float p = (now % 1500) / 1500.0f;
      float pr = 4.5f * (1 - p);
      if (pr > 1) {
        spr->fillCircle(38, (int)(96 - p * 30 + oy), (int)pr, C_DIM);
        spr->fillCircle(202, (int)(92 - p * 30 + oy), (int)pr, C_DIM);
      }
      break;
    }
    case FACE_CHARMED: {  // pink HEART eyes + blush + open mouth, admiring slide
      float dx = 6 * sinf(now * 0.0015f);
      float hs = 40 * (b > 0.5f ? 1.0f : b + 0.5f);
      heart(EYE_L + dx, EYE_Y - 4, hs, C_PINK);
      heart(EYE_R + dx, EYE_Y - 4, hs, C_PINK);
      blush(sinf(now * 0.003f) > 0 ? C_PINK : C_ROSE);   // blush pulses
      MOUTH(mouthOpen(MID + dx * 0.5f, 154, 12, 9));
      break;
    }
    case FACE_SPARKLE: {  // arcs + mouth + blush + white sparkle stars
      float sq = 1.0f - 0.35f * bump((now % 5000) / 5000.0f, 0.5f, 0.06f);
      arcUp(EYE_L, 104, 23, 9 * sq, C_CYAN);
      arcUp(EYE_R, 104, 23, 9 * sq, C_CYAN);
      blush();
      MOUTH(mouthOpen(MID, 152, 12, 9));
      float ss = 1.0f + 0.2f * max(0.0f, sinf(now * 0.003f));
      star4(40, 58, 11 * ss, now * 0.001f, 0, 0, C_WHITE);
      star4(200, 52, 8 * ss, -now * 0.0012f, 0, 0, C_CYAN);
      star4(186, 84, 5 * ss, now * 0.0015f, 0, 0, C_WHITE);
      break;
    }
    case FACE_WINK: {  // left eye open darting, right wink line, cyan smile
      float dx = 8 * sinf(now * 0.002f);
      ell(EYE_L + dx, EYE_Y, 20, 25 * b, C_CYAN);
      float wg = 2 * sinf(now * 0.01f);
      wl(EYE_R - 20, EYE_Y + wg, EYE_R + 20, EYE_Y - wg, 6, C_CYAN);
      MOUTH(arcDn(MID - 8, 152, 14, 5, C_PEACH));   // warm smirk
      break;
    }
    case FACE_STARRY: {  // gold stars morph to circles, undulating points
      float m = 0.5f + 0.5f * sinf(now * 0.002f);
      float core = 26 * (0.55f + 0.3f * (1 - m));
      star4(EYE_L, EYE_Y, 26 * (0.5f + 0.5f * m), now * 0.0005f, 0.12f, now * 0.006f, C_YELLOW);
      star4(EYE_R, EYE_Y, 26 * (0.5f + 0.5f * m), -now * 0.0005f, 0.12f, now * 0.006f + 2, C_YELLOW);
      circ(EYE_L, EYE_Y, core, C_YELLOW);
      circ(EYE_R, EYE_Y, core, C_YELLOW);
      break;
    }
    case FACE_LAUGH: {  // chuckle vibration, big rose mouth with tongue
      oy += 2 * sinf(now * 0.05f);
      float s = 0.95f + 0.05f * sinf(now * 0.0126f);
      arcUp(EYE_L, 100, 25 * s, 11 * s, C_CYAN);
      arcUp(EYE_R, 100, 25 * s, 11 * s, C_CYAN);
      blush();
      MOUTH(mouthOpen(MID, 156, 21 * s, 16 * s));
      break;
    }
    case FACE_EAGER: {  // arcs dart left/right/center + double blink squash
      float ph = fmodf(t, 3.2f), dx = 0; float th = 9;
      if (ph < 0.5f)      dx = -14 * (1 - powf(1 - ph / 0.5f, 3));
      else if (ph < 0.9f) dx = -14;
      else if (ph < 1.4f) dx = -14 + 28 * (1 - powf(1 - (ph - 0.9f) / 0.5f, 3));
      else if (ph < 1.8f) dx = 14;
      else if (ph < 2.2f) dx = 14 * (1 - (ph - 1.8f) / 0.4f);
      if ((ph > 2.4f && ph < 2.55f) || (ph > 2.7f && ph < 2.85f)) th = 4;  // double blink
      arcUp(EYE_L + dx, 102, 22, th, C_CYAN);
      arcUp(EYE_R + dx, 102, 22, th, C_CYAN);
      blush();
      MOUTH(mouthOpen(MID + dx * 0.4f, 152, 11, 9));
      break;
    }
    case FACE_ANTICIPATION: {  // side-glancing eyes slide up, peach smirk
      float ph = (now % 3000) / 3000.0f, y;
      if (ph < 0.85f) y = 122 - 18 * (ph / 0.85f);
      else            y = 104 + 18 * ((ph - 0.85f) / 0.15f) - 4 * sinf(PI * (ph - 0.85f) / 0.15f);
      oy += random(-1, 2) * 0.5f;
      ell(EYE_L, y, 22, 25 * b, C_CYAN);
      ell(EYE_R, y, 22, 25 * b, C_CYAN);
      if (b > 0.5f) {  // glancing sideways (sheet shows offset gaze)
        circ(EYE_L + 8, y - 4, 6, C_WHITE);
        circ(EYE_R + 8, y - 4, 6, C_WHITE);
      }
      blush();
      MOUTH(arcDn(MID + 10, 152, 12, 5, C_PEACH));
      break;
    }

    // ================= Row 2: Sadness & Anger =================
    case FACE_POUT: {  // flat-top sad eyes drift apart, cyan frown
      float dx = 5 * (0.5f + 0.5f * sinf(now * 0.001f));
      oy += 3 * (0.5f + 0.5f * sinf(now * 0.0008f));
      ell(EYE_L - dx, 107, 20, 21 * b, C_CYAN);
      ell(EYE_R + dx, 107, 20, 21 * b, C_CYAN);
      wl(EYE_L - dx - 22, 86, EYE_L - dx + 22, 92, 14, K);
      wl(EYE_R + dx - 22, 92, EYE_R + dx + 22, 86, 14, K);
      MOUTH(arcUp(MID, 165, 12, 5, C_CYAN));
      break;
    }
    case FACE_TEARY: {  // glossy eyes with glints, hanging tear, sloshing
      ell(EYE_L, 103, 21, 25 * b, C_CYAN);
      ell(EYE_R, 103, 21, 25 * b, C_CYAN);
      float sl = 4 * sinf(now * 0.003f);
      if (b > 0.6f) {
        ell(EYE_L + sl, 114, 16, 8, C_SKY);        // sloshing water line
        ell(EYE_R - sl, 114, 16, 8, C_SKY);
        circ(EYE_L - 7, 94, 5, C_WHITE);            // glints
        circ(EYE_R - 7, 94, 5, C_WHITE);
      }
      float tp = fmodf(now / 2400.0f, 1.0f);       // one slow tear on the left
      ell(EYE_L - 18, 130 + tp * 20, 4, 6 + 3 * tp, C_SKY);
      MOUTH(arcUp(MID, 168, 10, 4, C_CYAN));
      break;
    }
    case FACE_CRYING: {  // closed arcs + wide tear streams flowing down
      arcUp(EYE_L, 100, 22, 8, C_CYAN);
      arcUp(EYE_R, 100, 22, 8, C_CYAN);
      MOUTH(arcUp(MID, 172, 12, 5, C_CYAN));
      for (uint8_t i = 0; i < 2; i++) {            // stream: stacked stretching blobs
        float x = i ? EYE_R : EYE_L;
        for (uint8_t j = 0; j < 3; j++) {
          float p = fmodf((now + i * 700 + j * 460) / 1400.0f, 1.0f);
          float ry = 8 + 7 * p, rx = 8 * (1 - 0.55f * p);
          if (rx > 2) ell(x, 116 + p * 78, rx, ry, C_SKY);
        }
      }
      break;
    }
    case FACE_DISAPPOINTED: {  // drooped slant eyes, long blink, looks away
      float ph = fmodf(t, 5.0f), dx = 0, dy = 0; float w = 9;
      if (ph > 1.0f && ph < 2.0f) w = 4;                    // long slow blink
      else if (ph >= 2.0f) { dx = -8; dy = 5; }             // avoid eye contact
      wl(EYE_L - 20 + dx, 112 + dy, EYE_L + 20 + dx, 98 + dy, w, C_CYAN);
      wl(EYE_R - 20 + dx, 98 + dy, EYE_R + 20 + dx, 112 + dy, w, C_CYAN);
      MOUTH(arcUp(MID + dx, 166 + dy, 11, 4, C_CYAN));
      float tp = fmodf(now / 2800.0f, 1.0f);                // a single quiet tear
      ell(EYE_L - 16 + dx, 126 + dy + tp * 16, 3, 5, C_SKY);
      break;
    }
    case FACE_HEARTBROKEN: {  // big pink heart, pulsing crack
      float s = 62 * (1.0f + 0.05f * sinf(now * 0.004f));
      heart(MID, 108, s, C_PINK);
      float cw = 6 + 3 * sinf(now * 0.006f);
      wl(MID, 68, MID - 9, 95, cw, K);
      wl(MID - 9, 95, MID + 9, 122, cw, K);
      wl(MID + 9, 122, MID - 5, 152, cw, K);
      break;
    }
    case FACE_GRUMPY: {  // amber angry eyes, brows dip, darting
      float dx = sinf(now * 0.005f) > 0 ? 6 : -6;
      float dip = 5 * max(0.0f, sinf(now * 0.0015f));
      ell(EYE_L + dx, 108 + dip * 0.4f, 20, (22 - dip) * b, C_ORANGE);
      ell(EYE_R + dx, 108 + dip * 0.4f, 20, (22 - dip) * b, C_ORANGE);
      wl(EYE_L + dx - 24, 78 + dip, EYE_L + dx + 24, 92 + dip, 13, K);
      wl(EYE_R + dx - 24, 92 + dip, EYE_R + dx + 24, 78 + dip, 13, K);
      MOUTH(arcUp(MID + dx, 168, 13, 5, C_ORANGE));
      break;
    }
    case FACE_FURIOUS: {  // trembling orange-red slants + flame flicker
      ox += random(-2, 3); oy += random(-2, 3);     // positional jitter is allowed
      wl(EYE_L - 22, 88, EYE_L + 22, 108, 13, C_ORANGE);
      wl(EYE_R - 22, 108, EYE_R + 22, 88, 13, C_ORANGE);
      MOUTH(arcUp(MID, 172, 13, 6, C_RED));
      // flames used to jump to a random spot every frame (a 25-40 Hz flicker);
      // now each one drifts on slow sines, <= 1 Hz (§9.1 photosensitivity fix)
      for (uint8_t i = 0; i < 3; i++) {
        float ph = now * 0.0025f + i * 2.1f;
        float x = (i & 1) ? 194 + 12 * sinf(ph) : 46 + 12 * sinf(ph * 1.3f);
        float y = 74 + 10 * sinf(ph * 1.7f + i);
        circ(x, y, 3.5f + 1.5f * sinf(ph * 2.2f + i), C_ORANGE);
      }
      break;
    }
    case FACE_FUMING: {  // deep red angled eyes + open angry mouth + steam
      uint16_t c = sinf(now * 0.005f) > 0 ? C_RED : C_DRED;
      wl(EYE_L - 22, 84, EYE_L + 22, 106, 14, c);
      wl(EYE_R - 22, 106, EYE_R + 22, 84, 14, c);
      MOUTH(ell(MID, 166, 14, 10, c));             // open growling mouth
      for (uint8_t i = 0; i < 2; i++) {
        float p = fmodf((now + i * 900) / 1800.0f, 1.0f);
        float r = 9 * (1 - p) + 3;
        spr->fillCircle(i ? 218 : 22, (int)(140 - p * 65), (int)r, C_GREY);
      }
      break;
    }
    case FACE_ANNOYED: {  // amber half-lid twitch + deliberate eye roll
      float ph = fmodf(t, 6.0f), ex = EYE_L, ey = EYE_Y;
      if (ph > 4.0f && ph < 5.2f) {
        float a = PI * (ph - 4.0f) / 1.2f;
        ex = EYE_L + 16 * cosf(a); ey = EYE_Y - 15 * sinf(a);
      }
      ell(ex, ey, 20, 24 * b, C_ORANGE);
      float tw = ((now % 2000) < 80) ? 2 : 0;
      ell(EYE_R, 110 + tw, 21, 15, C_ORANGE);
      wl(EYE_R - 23, 96 + tw, EYE_R + 23, 96 + tw, 12, K);
      MOUTH(rrc(MID - 13, 162, 26, 6, 3, C_ORANGE));      // flat unimpressed mouth
      break;
    }
    case FACE_FLUSHED: {  // rosy overheated: pink eyes, heavy blush, swelling
      float s = 1.0f + 0.15f * sinf(now * 0.009f);
      float eb = ((now / 180) % 7 == 0) ? 0.2f : 1.0f;
      ell(EYE_L, EYE_Y, 21 * s, 25 * s * eb, C_PINK);
      ell(EYE_R, EYE_Y, 21 * s, 25 * s * eb, C_PINK);
      ell(EYE_L - 8, 140, 17, 7, C_ROSE);
      ell(EYE_R + 8, 140, 17, 7, C_ROSE);
      MOUTH(wl(MID - 10, 162, MID + 10, 160, 5, C_ROSE)); // small wavy mouth
      break;
    }

    // ============ Row 3: Curiosity, Playful & Affection ============
    case FACE_QUESTIONING: {  // one eye + one raised arch, skewed mouth
      ell(EYE_L, 112, 20, 19 * b, C_CYAN);
      arcUp(EYE_R, 86 + 4 * sinf(now * 0.003f), 18, 7, C_CYAN);
      MOUTH(wl(MID - 8, 158, MID + 10, 154, 5, C_CYAN));
      break;
    }
    case FACE_INVESTIGATING: {  // magnifying glass scans, small eye locked
      ell(EYE_L, 98, 12, 14 * b, C_CYAN);
      float mx = 140 + 30 * sinf(now * 0.0012f);
      circ(mx, 110, 33, C_WHITE);   // rim
      circ(mx, 110, 28, K);
      circ(mx, 110, 26, C_DIM);     // glass tint
      ell(mx, 110, 12, 15 * b, C_CYAN);                                // eye behind lens
      wl(mx + 24, 134, mx + 40, 154, 8, C_WHITE);                      // handle
      break;
    }
    case FACE_THOUGHTFUL: {  // eyes drift in figure-eight up-right, wavy mouth
      float dx = 5 * sinf(now * 0.0015f), dy = 3 * sinf(now * 0.003f);
      ell(EYE_L + 12 + dx, 88 + dy, 20, 18 * b, C_CYAN);
      ell(EYE_R + 12 + dx, 86 + dy, 20, 18 * b, C_CYAN);
      MOUTH(wl(MID - 10, 160, MID + 8, 163, 5, C_CYAN));
      break;
    }
    case FACE_CONFUSED: {  // ONE big pastel multicolor spiral (sheet), sine speed
      static float rot = 0;
      rot += 0.02f + 0.11f * (0.5f + 0.5f * sinf(now * 0.0008f));
      if (rot > TWO_PI) rot -= TWO_PI;
      static const uint16_t sc[4] = {C_PINK, C_VIOLET, C_SKY, C_TEAL};
      for (float a = 0.5f; a < 8.0f * PI; a += 0.16f) {
        float r = 2.3f * a;
        spr->fillCircle((int)(MID + ox + r * cosf(a + rot)),
                        (int)(112 + oy + r * sinf(a + rot)),
                        3, sc[(int)(a / TWO_PI) & 3]);
      }
      break;
    }
    case FACE_UPSIDE_DOWN:   // 46: WONDER's wide eyes; the panel itself is flipped (faceFlip)
    case FACE_WONDER: {  // massive eyes, breathing pupils, glints, pink o mouth
      ell(72, EYE_Y, 33, 39 * b, C_CYAN);
      ell(168, EYE_Y, 33, 39 * b, C_CYAN);
      if (b > 0.5f) {
        float pr = 10 + 4 * sinf(now * 0.002f);
        circ(72, EYE_Y, pr, C_WHITE);
        circ(168, EYE_Y, pr, C_WHITE);
        float ga = now * 0.001f;
        circ(72 + 16 * cosf(ga), EYE_Y - 16 * sinf(ga), 3, C_WHITE);
        circ(168 + 16 * cosf(ga + 2), EYE_Y - 16 * sinf(ga + 2), 3, C_WHITE);
      }
      MOUTH(ell(MID, 168, 8, 10, C_PINK));
      break;
    }
    case FACE_PLAYFUL: {  // winking wave + warm smirk
      float dl = -7 * fabsf(sinf(now * 0.006f));
      float dr = -7 * fabsf(sinf(now * 0.006f - 0.7f));
      ell(EYE_L, 104 + dl, 19, 23 * b, C_CYAN);
      arcUp(EYE_R, 106 + dr, 22, 8, C_CYAN);
      MOUTH(arcDn(MID - 6, 152, 15, 6, C_PEACH));
      break;
    }
    case FACE_MISCHIEVOUS: {  // sly squints slide to edge, pink tongue tip
      float ph = fmodf(t, 5.0f), dx = 0; float eb = 1.0f;
      if (ph < 1.5f)      dx = -44 * (1 - powf(1 - ph / 1.5f, 2));
      else if (ph < 2.5f) { dx = -44; if (ph > 1.9f && ph < 2.1f) eb = 0.2f; }
      else if (ph < 2.7f) dx = -44 * (1 - (ph - 2.5f) / 0.2f);
      ell(EYE_L + dx, 106, 22, 11 * eb, C_CYAN);
      ell(EYE_R + dx, 106, 22, 11 * eb, C_CYAN);
      wl(EYE_L + dx + 14, 96, EYE_L + dx + 24, 90, 5, C_CYAN);
      wl(EYE_R + dx + 14, 96, EYE_R + dx + 24, 90, 5, C_CYAN);
      MOUTH(rrc(MID + dx * 0.3f + 4, 150, 14, 16, 7, C_PINK));   // tongue tip at mouth corner
      break;
    }
    case FACE_CHEEKY: {  // squints + big two-tone tongue wiggling
      float ph = fmodf(t, 4.0f);
      float th = ((ph > 3.6f && ph < 3.72f) || (ph > 3.84f && ph < 3.96f)) ? 4 : 9;
      arcUp(EYE_L, 102, 23, th, C_CYAN);
      arcUp(EYE_R, 102, 23, th, C_CYAN);
      float wig = 4 * sinf(now * 0.008f);
      MOUTH(rrc(MID - 13 + wig, 144, 26, 32, 12, C_PINK);
            rrc(MID - 6 + wig, 150, 12, 22, 6, C_ROSE));         // tongue center line
      break;
    }
    case FACE_DERP: {  // independent goofy eyes + tongue lolling sideways
      float x1 = EYE_L + 10 * sinf(now * 0.0007f), y1 = 95 + 8 * sinf(now * 0.0009f);
      float x2 = EYE_R - 10 * sinf(now * 0.0007f), y2 = 115 - 8 * cosf(now * 0.0011f);
      ell(x1, y1, 17, 21, C_CYAN);
      ell(x2, y2, 26, 30, C_CYAN);
      float wig = 2 * sinf(now * 0.006f);
      MOUTH(rrc(MID + 8 + wig, 156, 16, 20, 8, C_PINK));         // tongue hangs to the side
      break;
    }
    case FACE_LOVING: {  // hearts beat ba-bum + open happy mouth
      float p = fmodf(now / 1400.0f, 1.0f);
      float beat = 0.3f * bump(p, 0.08f, 0.07f) + 0.18f * bump(p, 0.3f, 0.07f);
      float s = 38 * (1 + beat);
      if (beat > 0.1f) { heart(EYE_L, EYE_Y - 4, s * 1.22f, C_ROSE); heart(EYE_R, EYE_Y - 4, s * 1.22f, C_ROSE); }
      heart(EYE_L, EYE_Y - 4, s, C_PINK);
      heart(EYE_R, EYE_Y - 4, s, C_PINK);
      MOUTH(mouthOpen(MID, 158, 12, 9));
      break;
    }

    // ================= Row 4: Fatigue & Reactive =================
    case FACE_TIRED: {  // lids droop, snap open; small flat mouth
      float ph = fmodf(t, 3.4f);
      float cover = ph < 2.9f ? 0.35f + 0.5f * (ph / 2.9f) : 0.85f - 0.5f * ((ph - 2.9f) / 0.5f);
      ell(EYE_L, EYE_Y, 22, 26, C_CYAN);
      ell(EYE_R, EYE_Y, 22, 26, C_CYAN);
      spr->fillRect((int)(EYE_L + ox - 24), (int)(EYE_Y + oy - 27), 48, (int)(cover * 52), K);
      spr->fillRect((int)(EYE_R + ox - 24), (int)(EYE_Y + oy - 27), 48, (int)(cover * 52), K);
      MOUTH(rrc(MID - 10, 162, 20, 5, 2, C_CYAN));
      break;
    }
    case FACE_CHARGING:      // 44: SLEEPING + a battery glyph (unused: charging can't be sensed)
    case FACE_SLEEPING: {  // breathing closed lines + Zzz + tiny pink mouth
      float br = 1 + 0.5f * sinf(now * 0.0015f);
      rrc(EYE_L - 21, 104, 42, 5 + br, 3, C_CYAN);
      rrc(EYE_R - 21, 104, 42, 5 + br, 3, C_CYAN);
      spr->setTextColor(C_DIM, K);
      spr->setTextDatum(MC_DATUM);
      float p = fmodf(now / 2600.0f, 1.0f);
      spr->drawString("z", 158, (int)(88 - p * 34), 2);
      spr->drawString("Z", 176, (int)(70 - fmodf(p + 0.5f, 1.0f) * 34), 2);
      MOUTH(ell(MID, 158, 4, 3, C_ROSE));
      if (cur == FACE_CHARGING) battery(MID, 192, C_GREEN);
      break;
    }
    case FACE_YAWNING: {  // warm cream squints + big peach yawn balloon
      float ph = fmodf(t, 3.5f), m;
      if (ph < 1.2f)      m = ph / 1.2f;
      else if (ph < 2.0f) m = 1.0f + 0.03f * sinf(now * 0.06f);
      else if (ph < 2.6f) m = 1.0f - (ph - 2.0f) / 0.6f;
      else                m = 0;
      float sq = 1.0f - 0.5f * m;
      arcUp(EYE_L, 98, 20, 7 * sq + 2, C_CREAM);
      arcUp(EYE_R, 98, 20, 7 * sq + 2, C_CREAM);
      MOUTH(if (m > 0.05f) {
              ell(MID, 156, 12 + 10 * m, 6 + 26 * m, C_PEACH);
              ell(MID, 160, (12 + 10 * m) * 0.55f, (6 + 26 * m) * 0.5f, C_ROSE);
            } else rrc(MID - 8, 154, 16, 5, 2, C_CREAM));
      break;
    }
    case FACE_SLEEPY: {  // droopy dim arcs sink + peach drool drop
      float ph = 0.5f + 0.5f * sinf(now * 0.0008f);
      float wob = 2 * sinf(now * 0.004f);
      uint16_t c = ph > 0.55f ? C_DIM : C_CYAN;
      arcUp(EYE_L + wob, 106 + 9 * ph, 20, 7, c);
      arcUp(EYE_R - wob, 106 + 9 * ph, 20, 7, c);
      float dp = fmodf(now / 3000.0f, 1.0f);              // drool grows and drips
      MOUTH(ell(MID + 16, 156 + dp * 8, 5, 6 + 5 * dp, C_PEACH);
            rrc(MID - 10, 152, 18, 5, 2, c));
      break;
    }
    case FACE_SCARED: {  // pale trembling eyes with glints + wavy mouth
      ox += random(-2, 3); oy += random(-2, 3);
      float dx = 0, dy = 0;
      if ((now % 2200) < 250) {
        uint32_t h = now / 2200;
        dx = (h & 1) ? 18 : -18; dy = (h & 2) ? 12 : -12;
      }
      ell(94 + dx, 90 + dy, 15, 18 * b, C_CYAN);
      ell(146 + dx, 90 + dy, 15, 18 * b, C_CYAN);
      if (b > 0.5f) {
        circ(94 + dx - 4, 84 + dy, 4, C_WHITE);
        circ(146 + dx - 4, 84 + dy, 4, C_WHITE);
      }
      MOUTH(ell(MID + dx * 0.3f, 158, 11, 8, C_SKY));            // wavering open mouth
      break;
    }
    case FACE_BORED: {  // heavy-lidded eyes + flat mouth, sighs
      oy += 5 * bump(fmodf(t, 7.0f), 3.5f, 0.5f);
      float lid = (fmodf(t, 9.0f) > 8.6f) ? 0.85f : 0.55f;  // rare lethargic blink
      ell(EYE_L, EYE_Y, 21, 22, C_CYAN);
      ell(EYE_R, EYE_Y, 21, 22, C_CYAN);
      spr->fillRect((int)(EYE_L + ox - 23), (int)(EYE_Y + oy - 23), 46, (int)(lid * 44), K);
      spr->fillRect((int)(EYE_R + ox - 23), (int)(EYE_Y + oy - 23), 46, (int)(lid * 44), K);
      wl(EYE_L - 21, EYE_Y - 23 + lid * 44, EYE_L + 21, EYE_Y - 23 + lid * 44, 4, C_CYAN);  // lid lines
      wl(EYE_R - 21, EYE_Y - 23 + lid * 44, EYE_R + 21, EYE_Y - 23 + lid * 44, 4, C_CYAN);
      MOUTH(rrc(MID - 12, 162, 24, 5, 2, C_CYAN));
      break;
    }
    case FACE_EMBARRASSED: {  // downcast eyes dart away, blush spreads inward
      float dx = 6 * sinf(now * 0.0012f);
      arcUp(EYE_L + dx, 112, 18, 7, C_CYAN);              // shy downcast curves
      arcUp(EYE_R + dx, 112, 18, 7, C_CYAN);
      float sp = 0.5f + 0.5f * sinf(now * 0.002f);
      uint16_t bl = sp > 0.5f ? C_PINK : C_ROSE;
      ell(38, 124, 16 + 8 * sp, 22, bl);
      ell(202, 124, 16 + 8 * sp, 22, bl);
      MOUTH(wl(MID - 10 + dx, 158, MID + 10 + dx, 155, 5, C_ROSE));
      break;
    }
    case FACE_LOW_BATTERY:    // 45: HUNGRY, but the fish is an empty battery (one amber bar)
    case FACE_HUNGRY: {  // pleading eyes track a pale-teal floating fish
      float fx = MID + 8 * sinf(now * 0.002f);
      float fy = 170 + 3 * sinf(now * 0.0035f);
      float st = 5 * max(0.0f, sinf(now * 0.0015f));
      float track = (fx - MID) * 0.3f;
      ell(EYE_L + track, 96, 21, (26 + st) * b, C_TEAL);
      ell(EYE_R + track, 96, 21, (26 + st) * b, C_TEAL);
      if (cur == FACE_LOW_BATTERY) { battery(fx, fy, C_AMBER); break; }
      ell(fx, fy, 14, 8, C_TEAL);                          // little fish
      spr->fillTriangle(fx + ox + 12, fy + oy, fx + ox + 20, fy + oy - 6, fx + ox + 20, fy + oy + 6, C_TEAL);
      spr->fillCircle((int)(fx + ox - 6), (int)(fy + oy - 2), 1, K);
      star4(fx + 26, fy - 16, 5, now * 0.002f, 0, 0, C_WHITE);   // sparkle
      break;
    }

    // ================= Xelulu 39-48 (§9.2) =================
    case FACE_LISTENING: {  // round cyan eyes looking up, glints and sound arcs toward the voice
      int side = listenSide;
      ell(EYE_L, EYE_Y - 6, 22, 26 * b, C_CYAN);
      ell(EYE_R, EYE_Y - 6, 22, 26 * b, C_CYAN);
      if (b > 0.5f) {
        circ(EYE_L - 6 + 5 * side, EYE_Y - 17, 5, C_WHITE);
        circ(EYE_R - 6 + 5 * side, EYE_Y - 17, 5, C_WHITE);
      }
      MOUTH(rrc(MID - 7, 154, 14, 5, 2, C_CYAN));
      // two arcs beside the eye on the speaker's side; they change SIZE, not
      // brightness, at 0.9 Hz. Robot's left (+1) is the viewer's right.
      float s = 0.5f + 0.5f * sinf(now * 0.0057f);
      bool right = side >= 0;
      int cx = (int)((right ? EYE_R : EYE_L) + ox), cy = (int)(EYE_Y - 6 + oy);
      for (uint8_t k = 0; k < 2; k++) {
        int r = 34 + 9 * k + (int)(4 * s);
        if (right) spr->drawArc(cx, cy, r, r - 3, 235, 305, C_DIM, K, false);
        else       spr->drawArc(cx, cy, r, r - 3, 55, 125, C_DIM, K, false);
      }
      break;
    }
    case FACE_THINKING: {  // THOUGHTFUL's figure-eight + three dots lighting in turn (0.56 Hz each)
      float dx = 5 * sinf(now * 0.0015f), dy = 3 * sinf(now * 0.003f);
      ell(EYE_L + 12 + dx, 88 + dy, 20, 18 * b, C_CYAN);
      ell(EYE_R + 12 + dx, 86 + dy, 20, 18 * b, C_CYAN);
      MOUTH(wl(MID - 10, 160, MID + 8, 163, 5, C_CYAN));
      uint8_t lit = (now / 600) % 3;
      for (uint8_t i = 0; i < 3; i++) circ(MID - 14 + 14 * i, 186, 4, i == lit ? C_CYAN : C_DIM);
      break;
    }
    case FACE_COMFORT: {  // SMILE in warm amber, slow blink every 3 s, rose cheeks, double breathing
      oy += 1.8f * sinf(now * 0.0015f);
      float h = ((now % 3000) < 300) ? 4 : 32;
      rrc(EYE_L - 14, EYE_Y - 10 - h / 2, 28, h, 9, C_AMBER);
      rrc(EYE_R - 14, EYE_Y - 10 - h / 2, 28, h, 9, C_AMBER);
      blush(C_ROSE);
      MOUTH(arcDn(MID, 148, 13, 5, C_AMBER));
      break;
    }
    case FACE_BREATHE: {  // teal circles grow 20 % on a 4 s inhale, shrink on the exhale
      float ph = fmodf(now / 8000.0f, 1.0f);
      bool inhale = ph < 0.5f;
      float g = 0.5f - 0.5f * cosf(TWO_PI * ph);            // 0 -> 1 -> 0 over 8 s
      float r = 22 * (1.0f + 0.2f * g);
      ell(EYE_L, EYE_Y, r, r * b, C_TEAL);
      ell(EYE_R, EYE_Y, r, r * b, C_TEAL);
      MOUTH(if (inhale) rrc(MID - 8, 160, 16, 4, 2, C_TEAL);
            else ell(MID, 162, 6 + 3 * (1 - g), 7 + 3 * (1 - g), C_TEAL));   // "blow the candle"
      break;
    }
    case FACE_STORYTELLER: {  // kind half-lidded smiling eyes, rosy cheeks, warm cyan
      ell(EYE_L, EYE_Y, 22, 24 * b, C_WCYAN);
      ell(EYE_R, EYE_Y, 22, 24 * b, C_WCYAN);
      spr->fillRect((int)(EYE_L + ox - 24), (int)(EYE_Y + oy - 26), 48, 15, K);   // lid at 30 %
      spr->fillRect((int)(EYE_R + ox - 24), (int)(EYE_Y + oy - 26), 48, 15, K);
      ell(EYE_L, EYE_Y + 24, 26, 12, K);                    // smiling lower lid
      ell(EYE_R, EYE_Y + 24, 26, 12, K);
      blush(C_ROSE);
      MOUTH(arcDn(MID, 150, 12, 5, C_WCYAN));
      break;
    }
    case FACE_OFFLINE_SORRY: {  // soft pout eyes + a little sleeping cloud with a "z"
      ell(EYE_L, 107, 20, 21 * b, C_CYAN);
      ell(EYE_R, 107, 20, 21 * b, C_CYAN);
      wl(EYE_L - 22, 88, EYE_L + 22, 92, 10, K);
      wl(EYE_R - 22, 92, EYE_R + 22, 88, 10, K);
      MOUTH(arcUp(MID, 165, 10, 4, C_CYAN));
      float cy = 44 + 3 * sinf(now * 0.0015f);
      circ(176, cy, 12, C_GREY); circ(192, cy - 6, 14, C_GREY); circ(208, cy, 11, C_GREY);
      spr->fillRect((int)(176 + ox), (int)(cy + oy), 32, 11, C_GREY);
      spr->setTextColor(C_WHITE, C_GREY);
      spr->setTextDatum(MC_DATUM);
      spr->drawString("z", (int)(192 + ox), (int)(cy + oy), 2);
      break;
    }
    case FACE_UPDATING: {  // half-closed eyes + a smoothly rotating arc (motion, no on/off)
      ell(EYE_L, EYE_Y, 22, 24 * b, C_CYAN);
      ell(EYE_R, EYE_Y, 22, 24 * b, C_CYAN);
      spr->fillRect((int)(EYE_L + ox - 24), (int)(EYE_Y + oy - 26), 48, 25, K);
      spr->fillRect((int)(EYE_R + ox - 24), (int)(EYE_Y + oy - 26), 48, 25, K);
      MOUTH(rrc(MID - 8, 150, 16, 5, 2, C_CYAN));
      uint32_t a = (now / 4) % 360;                         // 0.7 rev/s
      spr->drawArc((int)(MID + ox), (int)(186 + oy), 14, 10, a, (a + 100) % 360, C_GREEN, K, false);
      break;
    }
    default: break;
  }
}

// ---------------------------------------------------------------------------
// Push: 8-bit sprite -> brightness LUT -> zoom -> RGB565 bands -> DMA
// ---------------------------------------------------------------------------
#define BAND_ROWS   20
#define ROT_UPRIGHT 3        // as mounted (bench 2026-10-31): TFT_eSPI's rotation 1 shows the face upside down
#define FACE_ZOOM   1.5f     // [CAL] panel px per canvas px. 1 = Eillik's size in the middle 240 px (it filled
                             // only about half the panel); > 1 crops the canvas edges and uses the full width
#define ZOOM_CX     120      // [CAL] canvas point shown at the panel centre
#define ZOOM_CY     120
static uint16_t lut[256];
static uint8_t lutPct = 0;
static volatile uint8_t brightPct = 100;
static uint16_t* bandBuf[2];
static bool dmaOk = false;
static int16_t xmap[320], ymap[240];   // panel column / row -> canvas column / row, -1 = off the canvas (black)

static void buildZoom() {
  for (int x = 0; x < 320; x++) {
    int c = (int)floorf(ZOOM_CX + (x + 0.5f - 160) / FACE_ZOOM);
    xmap[x] = c < 0 || c > 239 ? -1 : c;
  }
  for (int y = 0; y < 240; y++) {
    int r = (int)floorf(ZOOM_CY + (y + 0.5f - 120) / FACE_ZOOM);
    ymap[y] = r < 0 || r > 239 ? -1 : r;
  }
}

static void buildLut(uint8_t pct) {
  for (int v = 0; v < 256; v++) {
    uint32_t r5 = ((v >> 5) * 31 + 3) / 7, g6 = (((v >> 2) & 7) * 63 + 3) / 7, b5 = ((v & 3) * 31 + 1) / 3;
    uint16_t c = (r5 * pct / 100) << 11 | (g6 * pct / 100) << 5 | (b5 * pct / 100);
    lut[v] = c >> 8 | c << 8;   // pre-swapped: the SPI shifts out MSB first
  }
  lutPct = pct;
}

static inline uint16_t rgb565sw(uint32_t rgb, float k) {
  uint16_t c = (uint16_t)(((rgb >> 16) & 0xFF) * k) >> 3 << 11 |
               (uint16_t)(((rgb >> 8) & 0xFF) * k) >> 2 << 5 |
               (uint16_t)((rgb & 0xFF) * k) >> 3;
  return c >> 8 | c << 8;
}

// panel columns x0 .. x0+w-1, all 240 rows; the side bands are left alone while a glow shows there
static void pushCanvas(int x0, int w) {
  const uint8_t* src = (const uint8_t*)spr->getPointer();
  tft.setAddrWindow(x0, 0, w, 240);
  for (int band = 0; band < 240 / BAND_ROWS; band++) {
    uint16_t* dst = bandBuf[band & 1];
    for (int y = band * BAND_ROWS; y < (band + 1) * BAND_ROWS; y++) {
      const uint8_t* row = ymap[y] < 0 ? nullptr : src + ymap[y] * 240;
      for (int x = x0; x < x0 + w; x++) *dst++ = row && xmap[x] >= 0 ? lut[row[xmap[x]]] : 0;
    }
    if (dmaOk) tft.pushPixelsDMA(bandBuf[band & 1], BAND_ROWS * w);   // waits for the previous band first
    else tft.pushPixels(bandBuf[band & 1], BAND_ROWS * w);
  }
  if (dmaOk) {
    tft.dmaWait();
    // TFT_eSPI 2.5.43's end-of-DMA callback clears SPI_DMA_CONF_REG(spi_host), a host enum where IDF 5
    // wants a peripheral number: on the S3 it clears SPI2 instead of our port. Ours stayed in DMA mode,
    // the next setAddrWindow went out as garbage, and the panel kept the first frame (bench 2026-10-31).
    WRITE_PERI_REG(SPI_DMA_CONF_REG(SPI_PORT), 0);
  }
}

// one 40x120 side-band quarter: brightest at the panel edge, fading toward the face
static void drawBand(int x0, int y0, uint32_t rgb, bool leftSide) {
  uint16_t row[40];
  for (int c = 0; c < 40; c++) {
    int d = leftSide ? c : 39 - c;
    row[c] = rgb565sw(rgb, 1.0f - 0.75f * d / 39.0f);
  }
  tft.setAddrWindow(x0, y0, 40, 120);
  for (int y = 0; y < 120; y++) tft.pushPixels(row, 40);
}

// vignette mode (GLOW_MODE 1): soft corners inside the canvas, for a visor that hides the bands
static uint16_t rgbTo565(uint32_t rgb, float k) {
  return (uint16_t)(((rgb >> 16) & 0xFF) * k) >> 3 << 11 | (uint16_t)(((rgb >> 8) & 0xFF) * k) >> 2 << 5 |
         (uint16_t)((rgb & 0xFF) * k) >> 3;
}
static void vignette(uint32_t rgb, bool upper) {
  if (!rgb) return;
  // the corners of what the panel shows: the zoom crops the canvas
  int xl = max(0, (int)(ZOOM_CX - 160 / FACE_ZOOM)), xr = min(239, (int)(ZOOM_CX + 160 / FACE_ZOOM));
  int y = upper ? max(0, (int)(ZOOM_CY - 120 / FACE_ZOOM)) : min(239, (int)(ZOOM_CY + 120 / FACE_ZOOM));
  for (int k = 3; k >= 1; k--) {
    uint16_t c = rgbTo565(rgb, 0.35f * (4 - k));
    spr->fillCircle(xl, y, 12 * k, c);
    spr->fillCircle(xr, y, 12 * k, c);
  }
}

// ---------------------------------------------------------------------------
static volatile bool flipReq = false, flipState = false, panelReq = true, panelOn = true;
static uint16_t fps = 0;

void faceBegin() {
  pinMode(PIN_TFT_EN, OUTPUT);         // backlight on (as built: GPIO45). Boot is >0.5 s in by now,
  digitalWrite(PIN_TFT_EN, HIGH);      // the settle Display_test_code gives the panel first
  tft.init();
  tft.setRotation(ROT_UPRIGHT);        // landscape 320x240
  tft.fillScreen(TFT_BLACK);
  dmaOk = tft.initDMA();               // before createSprite: keeps the sprite in internal RAM
  spr = new TFT_eSprite(&tft);
  spr->setColorDepth(8);
  spr->setAttribute(PSRAM_ENABLE, false);
  if (!spr->createSprite(240, 240)) Serial.println("[face] sprite allocation FAILED");
  for (auto& b : bandBuf) b = (uint16_t*)heap_caps_malloc(BAND_ROWS * 320 * 2, MALLOC_CAP_DMA);
  buildLut(brightPct);
  buildZoom();
  blinkNext = millis() + 2500;
  Serial.printf("[face] ST7789 320x240, canvas 240x240 zoom %.2f, DMA %s\n", FACE_ZOOM, dmaOk ? "on" : "off");
}

void faceSet(FaceId f) {
  if (f >= FACE_COUNT) return;
  if (S.uploading && f != FACE_LISTENING) return;   // §5.9: while mic audio leaves, nothing hides LISTENING
  cur = f;
  setT = millis();
  blinkPhase = 1;
  blinkT = setT;
  Serial.printf("face %d %s\n", f + 1, FACE_NAMES[f]);
}

FaceId faceCurrent() { return cur; }

int faceFromName(const char* name) {
  for (int i = 0; i < FACE_COUNT; i++) {
    const char* a = FACE_NAMES[i];
    const char* b = name;
    while (*a && *b) {
      char ca = *a == ' ' ? '_' : (char)tolower(*a);
      if (ca != *b) break;
      a++; b++;
    }
    if (!*a && !*b) return i;
  }
  return -1;
}

__attribute__((weak)) void faceOverlay(TFT_eSprite&) {}

void faceGaze(int8_t x, int8_t y) { gazeTx = x; gazeTy = y; }
void faceEyeTilt(int8_t px) { tiltTarget = px; }
void faceListenSide(int8_t side) { listenSide = side; }
void faceSpeaking(bool on) { speaking = on; }
void faceEnvelope(float e) { envelope = e; }
void faceBlink() { blinkReq = true; }
void faceSquash() { squashReq = true; }
void faceBrightness(uint8_t pct) { brightPct = pct > 100 ? 100 : pct; }
void faceFlip(bool upsideDown) { flipState = upsideDown; flipReq = true; }
void facePanel(bool on) { panelOn = on; panelReq = true; }
uint16_t faceFps() { return fps; }

void faceUpdate(uint32_t now) {
  if (!spr) return;
  static uint32_t lastFrame = 0, lastGlow = 0, fpsT = 0, frames = 0, lastNow = 0;
  static GlowFrame shown = {1, 1, 1};
  if (now - lastFrame < 33) return;    // 30 fps target; late frames are skipped, never caught up
  float dt = lastNow ? (now - lastNow) : 33;
  lastFrame = now; lastNow = now;

  if (panelReq) {
    panelReq = false;
    tft.writecommand(panelOn ? 0x29 : 0x28);   // ST7789 DISPON / DISPOFF
  }
  if (!panelOn) return;
  if (flipReq) {
    flipReq = false;
    tft.setRotation(flipState ? ROT_UPRIGHT ^ 2 : ROT_UPRIGHT);   // 180 deg for free (1 <-> 3)
    tft.fillScreen(TFT_BLACK);
    shown = {1, 1, 1};
  }
  if (brightPct != lutPct) buildLut(brightPct);

  if (blinkReq) { blinkReq = false; blinkPhase = 1; blinkT = now; }
  blinkEngine(now);
  float k = min(1.0f, dt / 150.0f);            // gaze eases over ~150 ms
  gzx += (gazeTx - gzx) * k;
  gzy += (gazeTy - gzy) * k;
  tiltPx += (tiltTarget - tiltPx) * k;
  if (squashReq) { squashReq = false; bs = min(bs, 0.8f); rxs = 1.15f; }
  ox = gzx;
  oy = 1.8f * sinf(now * 0.0015f) + gzy;       // global breathing bob

  spr->fillSprite(K);
  mouthDrawn = false;
  draw(now);
  if (speaking && !mouthDrawn) { mouthBegin(); mouthEnd(); }   // faces without a mouth speak too

  GlowFrame g = glowEval(now);
  if (g.cheeks) {                              // cloud `led cheeks`: blush in the commanded colour
    uint16_t c = rgbTo565(g.cheeks, 1.0f);
    ell(EYE_L - 6, 138, 15, 6, c);
    ell(EYE_R + 6, 138, 15, 6, c);
  }
  if (cal.glowMode == 1) { vignette(g.upper, true); vignette(g.lower, false); }
  faceOverlay(*spr);

  tft.startWrite();
  // zoomed, the face fills the panel unless a side glow is showing; Eillik's size always stays in the middle
  if (FACE_ZOOM > 1 && !(cal.glowMode == 0 && (g.upper || g.lower))) pushCanvas(0, 320);
  else pushCanvas(40, 240);
  // side bands: only when a colour changed, at most 10 Hz (glows are <= 2 Hz anyway)
  if (cal.glowMode == 0 && now - lastGlow >= 100 && (g.upper != shown.upper || g.lower != shown.lower)) {
    lastGlow = now;
    drawBand(0, 0, g.upper, true);   drawBand(280, 0, g.upper, false);
    drawBand(0, 120, g.lower, true); drawBand(280, 120, g.lower, false);
    shown = g;
  }
  tft.endWrite();

  frames++;
  if (now - fpsT >= 1000) { fps = frames; frames = 0; fpsT = now; }
}
