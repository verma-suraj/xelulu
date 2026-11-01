#include "EilikFace.h"

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

static TFT_eSprite* spr = nullptr;
static FaceExpr cur = FACE_SMILE;
static unsigned long setT = 0;
static const char* label = nullptr;

// per-frame globals: breathing offset, blink scale, squash widen
static float ox = 0, oy = 0;
static float bs = 1.0f;
static float rxs = 1.0f;

// ---------- primitives (all apply ox/oy) ----------
static void ell(float x, float y, float rx, float ry, uint16_t c) {
  if (ry < 2) ry = 2;
  spr->fillEllipse((int)(x + ox), (int)(y + oy), (int)(rx * rxs), (int)ry, c);
}
static void rrc(float x, float y, float w, float h, float r, uint16_t c) {
  spr->fillRoundRect((int)(x + ox), (int)(y + oy), (int)w, (int)h, (int)r, c);
}
static void wl(float x0, float y0, float x1, float y1, float w, uint16_t c) {
  spr->drawWideLine(x0 + ox, y0 + oy, x1 + ox, y1 + oy, w, c, K);
}
// n-shaped arc (happy eye / frown mouth)
static void arcUp(float x, float y, float r, float t, uint16_t c) {
  int X = x + ox, Y = y + oy;
  spr->fillCircle(X, Y, (int)r, c);
  spr->fillCircle(X, Y, (int)(r - t), K);
  spr->fillRect(X - r - 1, Y, 2 * r + 3, r + 2, K);
}
// u-shaped arc (smile mouth)
static void arcDn(float x, float y, float r, float t, uint16_t c) {
  int X = x + ox, Y = y + oy;
  spr->fillCircle(X, Y, (int)r, c);
  spr->fillCircle(X, Y, (int)(r - t), K);
  spr->fillRect(X - r - 1, Y - r - 1, 2 * r + 3, r + 2, K);
}
static void heart(float x, float y, float s, uint16_t c) {
  spr->fillCircle((int)(x + ox - 0.32f * s), (int)(y + oy - 0.22f * s), (int)(0.36f * s), c);
  spr->fillCircle((int)(x + ox + 0.32f * s), (int)(y + oy - 0.22f * s), (int)(0.36f * s), c);
  spr->fillTriangle(x + ox - 0.64f * s, y + oy - 0.02f * s,
                    x + ox + 0.64f * s, y + oy - 0.02f * s,
                    x + ox, y + oy + 0.75f * s, c);
}
static void star4(float x, float y, float r, float rot, float und, float ph, uint16_t c) {
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

// ---------- the 38 faces, shapes & colors per Animation sheet.png ----------
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
      arcDn(MID, 148, 13, 5, C_CYAN);
      break;
    }
    case FACE_GIDDY: {  // cyan arcs + open pink mouth + blush, tandem bounce
      float bp = fabsf(sinf(now * 0.012f));
      oy -= 7 * bp;
      float sq = bp > 0.85f ? 1.15f : 1.0f;
      arcUp(EYE_L, 102, 23 * sq, 9, C_CYAN);
      arcUp(EYE_R, 102, 23 * sq, 9, C_CYAN);
      blush();
      mouthOpen(MID, 152, 13, 10);
      break;
    }
    case FACE_ECSTATIC: {  // heartbeat pulse + particles, bigger mouth
      float s = 1.0f + 0.08f * sinf(now * 0.008f);
      arcUp(EYE_L, 102, 24 * s, 11 * s, C_CYAN);
      arcUp(EYE_R, 102, 24 * s, 11 * s, C_CYAN);
      blush();
      mouthOpen(MID, 154, 17 * s, 13 * s);
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
      mouthOpen(MID + dx * 0.5f, 154, 12, 9);
      break;
    }
    case FACE_SPARKLE: {  // arcs + mouth + blush + white sparkle stars
      float sq = 1.0f - 0.35f * bump((now % 5000) / 5000.0f, 0.5f, 0.06f);
      arcUp(EYE_L, 104, 23, 9 * sq, C_CYAN);
      arcUp(EYE_R, 104, 23, 9 * sq, C_CYAN);
      blush();
      mouthOpen(MID, 152, 12, 9);
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
      arcDn(MID - 8, 152, 14, 5, C_PEACH);   // warm smirk
      break;
    }
    case FACE_STARRY: {  // gold stars morph to circles, undulating points
      float m = 0.5f + 0.5f * sinf(now * 0.002f);
      float core = 26 * (0.55f + 0.3f * (1 - m));
      star4(EYE_L, EYE_Y, 26 * (0.5f + 0.5f * m), now * 0.0005f, 0.12f, now * 0.006f, C_YELLOW);
      star4(EYE_R, EYE_Y, 26 * (0.5f + 0.5f * m), -now * 0.0005f, 0.12f, now * 0.006f + 2, C_YELLOW);
      spr->fillCircle((int)(EYE_L + ox), (int)(EYE_Y + oy), (int)core, C_YELLOW);
      spr->fillCircle((int)(EYE_R + ox), (int)(EYE_Y + oy), (int)core, C_YELLOW);
      break;
    }
    case FACE_LAUGH: {  // chuckle vibration, big rose mouth with tongue
      oy += 2 * sinf(now * 0.05f);
      float s = 0.95f + 0.05f * sinf(now * 0.0126f);
      arcUp(EYE_L, 100, 25 * s, 11 * s, C_CYAN);
      arcUp(EYE_R, 100, 25 * s, 11 * s, C_CYAN);
      blush();
      mouthOpen(MID, 156, 21 * s, 16 * s);
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
      mouthOpen(MID + dx * 0.4f, 152, 11, 9);
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
        spr->fillCircle((int)(EYE_L + 8 + ox), (int)(y - 4 + oy), 6, C_WHITE);
        spr->fillCircle((int)(EYE_R + 8 + ox), (int)(y - 4 + oy), 6, C_WHITE);
      }
      blush();
      arcDn(MID + 10, 152, 12, 5, C_PEACH);
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
      arcUp(MID, 165, 12, 5, C_CYAN);
      break;
    }
    case FACE_TEARY: {  // glossy eyes with glints, hanging tear, sloshing
      ell(EYE_L, 103, 21, 25 * b, C_CYAN);
      ell(EYE_R, 103, 21, 25 * b, C_CYAN);
      float sl = 4 * sinf(now * 0.003f);
      if (b > 0.6f) {
        ell(EYE_L + sl, 114, 16, 8, C_SKY);        // sloshing water line
        ell(EYE_R - sl, 114, 16, 8, C_SKY);
        spr->fillCircle((int)(EYE_L - 7 + ox), (int)(94 + oy), 5, C_WHITE);  // glints
        spr->fillCircle((int)(EYE_R - 7 + ox), (int)(94 + oy), 5, C_WHITE);
      }
      float tp = fmodf(now / 2400.0f, 1.0f);       // one slow tear on the left
      ell(EYE_L - 18, 130 + tp * 20, 4, 6 + 3 * tp, C_SKY);
      arcUp(MID, 168, 10, 4, C_CYAN);
      break;
    }
    case FACE_CRYING: {  // closed arcs + wide tear streams flowing down
      arcUp(EYE_L, 100, 22, 8, C_CYAN);
      arcUp(EYE_R, 100, 22, 8, C_CYAN);
      arcUp(MID, 172, 12, 5, C_CYAN);
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
      arcUp(MID + dx, 166 + dy, 11, 4, C_CYAN);
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
      arcUp(MID + dx, 168, 13, 5, C_ORANGE);
      break;
    }
    case FACE_FURIOUS: {  // trembling orange-red slants + flame flicker
      ox += random(-2, 3); oy += random(-2, 3);
      wl(EYE_L - 22, 88, EYE_L + 22, 108, 13, C_ORANGE);
      wl(EYE_R - 22, 108, EYE_R + 22, 88, 13, C_ORANGE);
      arcUp(MID, 172, 13, 6, C_RED);
      for (uint8_t i = 0; i < 3; i++)
        spr->fillCircle(random(0, 2) ? random(34, 58) : random(182, 206),
                        random(64, 84), random(2, 5), C_ORANGE);
      break;
    }
    case FACE_FUMING: {  // deep red angled eyes + open angry mouth + steam
      uint16_t c = sinf(now * 0.005f) > 0 ? C_RED : C_DRED;
      wl(EYE_L - 22, 84, EYE_L + 22, 106, 14, c);
      wl(EYE_R - 22, 106, EYE_R + 22, 84, 14, c);
      ell(MID, 166, 14, 10, c);                    // open growling mouth
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
      rrc(MID - 13, 162, 26, 6, 3, C_ORANGE);      // flat unimpressed mouth
      break;
    }
    case FACE_FLUSHED: {  // rosy overheated: pink eyes, heavy blush, swelling
      float s = 1.0f + 0.15f * sinf(now * 0.009f);
      float eb = ((now / 180) % 7 == 0) ? 0.2f : 1.0f;
      ell(EYE_L, EYE_Y, 21 * s, 25 * s * eb, C_PINK);
      ell(EYE_R, EYE_Y, 21 * s, 25 * s * eb, C_PINK);
      ell(EYE_L - 8, 140, 17, 7, C_ROSE);
      ell(EYE_R + 8, 140, 17, 7, C_ROSE);
      wl(MID - 10, 162, MID + 10, 160, 5, C_ROSE); // small wavy mouth
      break;
    }

    // ============ Row 3: Curiosity, Playful & Affection ============
    case FACE_QUESTIONING: {  // one eye + one raised arch, skewed mouth
      ell(EYE_L, 112, 20, 19 * b, C_CYAN);
      arcUp(EYE_R, 86 + 4 * sinf(now * 0.003f), 18, 7, C_CYAN);
      wl(MID - 8, 158, MID + 10, 154, 5, C_CYAN);
      break;
    }
    case FACE_INVESTIGATING: {  // magnifying glass scans, small eye locked
      ell(EYE_L, 98, 12, 14 * b, C_CYAN);
      float mx = 140 + 30 * sinf(now * 0.0012f);
      spr->fillCircle((int)(mx + ox), (int)(110 + oy), 33, C_WHITE);   // rim
      spr->fillCircle((int)(mx + ox), (int)(110 + oy), 28, K);
      spr->fillCircle((int)(mx + ox), (int)(110 + oy), 26, C_DIM);     // glass tint
      ell(mx, 110, 12, 15 * b, C_CYAN);                                // eye behind lens
      wl(mx + 24, 134, mx + 40, 154, 8, C_WHITE);                      // handle
      break;
    }
    case FACE_THOUGHTFUL: {  // eyes drift in figure-eight up-right, wavy mouth
      float dx = 5 * sinf(now * 0.0015f), dy = 3 * sinf(now * 0.003f);
      ell(EYE_L + 12 + dx, 88 + dy, 20, 18 * b, C_CYAN);
      ell(EYE_R + 12 + dx, 86 + dy, 20, 18 * b, C_CYAN);
      wl(MID - 10, 160, MID + 8, 163, 5, C_CYAN);
      break;
    }
    case FACE_CONFUSED: {  // ONE big pastel multicolor spiral (sheet), sine speed
      static float rot = 0;
      rot += 0.02f + 0.11f * (0.5f + 0.5f * sinf(now * 0.0008f));
      static const uint16_t sc[4] = {C_PINK, C_VIOLET, C_SKY, C_TEAL};
      for (float a = 0.5f; a < 8.0f * PI; a += 0.16f) {
        float r = 2.3f * a;
        spr->fillCircle((int)(MID + ox + r * cosf(a + rot)),
                        (int)(112 + oy + r * sinf(a + rot)),
                        3, sc[(int)(a / TWO_PI) & 3]);
      }
      break;
    }
    case FACE_WONDER: {  // massive eyes, breathing pupils, glints, pink o mouth
      ell(72, EYE_Y, 33, 39 * b, C_CYAN);
      ell(168, EYE_Y, 33, 39 * b, C_CYAN);
      if (b > 0.5f) {
        float pr = 10 + 4 * sinf(now * 0.002f);
        spr->fillCircle((int)(72 + ox), (int)(EYE_Y + oy), (int)pr, C_WHITE);
        spr->fillCircle((int)(168 + ox), (int)(EYE_Y + oy), (int)pr, C_WHITE);
        float ga = now * 0.001f;
        spr->fillCircle((int)(72 + ox + 16 * cosf(ga)), (int)(EYE_Y + oy - 16 * sinf(ga)), 3, C_WHITE);
        spr->fillCircle((int)(168 + ox + 16 * cosf(ga + 2)), (int)(EYE_Y + oy - 16 * sinf(ga + 2)), 3, C_WHITE);
      }
      ell(MID, 168, 8, 10, C_PINK);
      break;
    }
    case FACE_PLAYFUL: {  // winking wave + warm smirk
      float dl = -7 * fabsf(sinf(now * 0.006f));
      float dr = -7 * fabsf(sinf(now * 0.006f - 0.7f));
      ell(EYE_L, 104 + dl, 19, 23 * b, C_CYAN);
      arcUp(EYE_R, 106 + dr, 22, 8, C_CYAN);
      arcDn(MID - 6, 152, 15, 6, C_PEACH);
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
      rrc(MID + dx * 0.3f + 4, 150, 14, 16, 7, C_PINK);   // tongue tip at mouth corner
      break;
    }
    case FACE_CHEEKY: {  // squints + big two-tone tongue wiggling
      float ph = fmodf(t, 4.0f);
      float th = ((ph > 3.6f && ph < 3.72f) || (ph > 3.84f && ph < 3.96f)) ? 4 : 9;
      arcUp(EYE_L, 102, 23, th, C_CYAN);
      arcUp(EYE_R, 102, 23, th, C_CYAN);
      float wig = 4 * sinf(now * 0.008f);
      rrc(MID - 13 + wig, 144, 26, 32, 12, C_PINK);
      rrc(MID - 6 + wig, 150, 12, 22, 6, C_ROSE);         // tongue center line
      break;
    }
    case FACE_DERP: {  // independent goofy eyes + tongue lolling sideways
      float x1 = EYE_L + 10 * sinf(now * 0.0007f), y1 = 95 + 8 * sinf(now * 0.0009f);
      float x2 = EYE_R - 10 * sinf(now * 0.0007f), y2 = 115 - 8 * cosf(now * 0.0011f);
      ell(x1, y1, 17, 21, C_CYAN);
      ell(x2, y2, 26, 30, C_CYAN);
      float wig = 2 * sinf(now * 0.006f);
      rrc(MID + 8 + wig, 156, 16, 20, 8, C_PINK);         // tongue hangs to the side
      break;
    }
    case FACE_LOVING: {  // hearts beat ba-bum + open happy mouth
      float p = fmodf(now / 1400.0f, 1.0f);
      float beat = 0.3f * bump(p, 0.08f, 0.07f) + 0.18f * bump(p, 0.3f, 0.07f);
      float s = 38 * (1 + beat);
      if (beat > 0.1f) { heart(EYE_L, EYE_Y - 4, s * 1.22f, C_ROSE); heart(EYE_R, EYE_Y - 4, s * 1.22f, C_ROSE); }
      heart(EYE_L, EYE_Y - 4, s, C_PINK);
      heart(EYE_R, EYE_Y - 4, s, C_PINK);
      mouthOpen(MID, 158, 12, 9);
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
      rrc(MID - 10, 162, 20, 5, 2, C_CYAN);
      break;
    }
    case FACE_SLEEPING: {  // breathing closed lines + Zzz + tiny pink mouth
      float br = 1 + 0.5f * sinf(now * 0.0015f);
      rrc(EYE_L - 21, 104, 42, 5 + br, 3, C_CYAN);
      rrc(EYE_R - 21, 104, 42, 5 + br, 3, C_CYAN);
      spr->setTextColor(C_DIM, K);
      spr->setTextDatum(MC_DATUM);
      float p = fmodf(now / 2600.0f, 1.0f);
      spr->drawString("z", 158, (int)(88 - p * 34), 2);
      spr->drawString("Z", 176, (int)(70 - fmodf(p + 0.5f, 1.0f) * 34), 2);
      ell(MID, 158, 4, 3, C_ROSE);
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
      if (m > 0.05f) {
        ell(MID, 156, 12 + 10 * m, 6 + 26 * m, C_PEACH);
        ell(MID, 160, (12 + 10 * m) * 0.55f, (6 + 26 * m) * 0.5f, C_ROSE);
      } else rrc(MID - 8, 154, 16, 5, 2, C_CREAM);
      break;
    }
    case FACE_SLEEPY: {  // droopy dim arcs sink + peach drool drop
      float ph = 0.5f + 0.5f * sinf(now * 0.0008f);
      float wob = 2 * sinf(now * 0.004f);
      uint16_t c = ph > 0.55f ? C_DIM : C_CYAN;
      arcUp(EYE_L + wob, 106 + 9 * ph, 20, 7, c);
      arcUp(EYE_R - wob, 106 + 9 * ph, 20, 7, c);
      float dp = fmodf(now / 3000.0f, 1.0f);              // drool grows and drips
      ell(MID + 16, 156 + dp * 8, 5, 6 + 5 * dp, C_PEACH);
      rrc(MID - 10, 152, 18, 5, 2, c);
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
        spr->fillCircle((int)(94 + dx + ox - 4), (int)(84 + dy + oy), 4, C_WHITE);
        spr->fillCircle((int)(146 + dx + ox - 4), (int)(84 + dy + oy), 4, C_WHITE);
      }
      ell(MID + dx * 0.3f, 158, 11, 8, C_SKY);            // wavering open mouth
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
      rrc(MID - 12, 162, 24, 5, 2, C_CYAN);
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
      wl(MID - 10 + dx, 158, MID + 10 + dx, 155, 5, C_ROSE);
      break;
    }
    case FACE_HUNGRY: {  // pleading eyes track a pale-teal floating fish
      float fx = MID + 8 * sinf(now * 0.002f);
      float fy = 170 + 3 * sinf(now * 0.0035f);
      float st = 5 * max(0.0f, sinf(now * 0.0015f));
      float track = (fx - MID) * 0.3f;
      ell(EYE_L + track, 96, 21, (26 + st) * b, C_TEAL);
      ell(EYE_R + track, 96, 21, (26 + st) * b, C_TEAL);
      ell(fx, fy, 14, 8, C_TEAL);                          // little fish
      spr->fillTriangle(fx + ox + 12, fy + oy, fx + ox + 20, fy + oy - 6, fx + ox + 20, fy + oy + 6, C_TEAL);
      spr->fillCircle((int)(fx + ox - 6), (int)(fy + oy - 2), 1, K);
      star4(fx + 26, fy - 16, 5, now * 0.002f, 0, 0, C_WHITE);   // sparkle
      break;
    }
    default: break;
  }
}

void faceLabel(const char* name) { label = name; }

void faceBegin(TFT_eSprite* sprite) {
  spr = sprite;
  blinkNext = millis() + 2500;
}

void faceSet(FaceExpr f) {
  if (f >= FACE_COUNT) return;
  cur = f;
  setT = millis();
  blinkPhase = 1;
  blinkT = setT;
}

void faceUpdate() {
  if (!spr) return;
  static unsigned long lastFrame = 0;
  unsigned long now = millis();
  if (now - lastFrame < 25) return;   // ~40 fps
  lastFrame = now;

  blinkEngine(now);
  ox = 0;
  oy = 1.8f * sinf(now * 0.0015f);    // global breathing bob

  spr->fillSprite(K);
  draw(now);
  if (label) {
    spr->setTextDatum(MC_DATUM);
    spr->setTextColor(C_CYAN, K);
    spr->drawString(label, MID, 208, 2);
  }
  spr->pushSprite(0, 0);
}
