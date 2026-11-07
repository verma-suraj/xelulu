#include "Glow.h"
#include "../../Contract.h"

// Region slots: 0 = ears (upper bands), 1 = belly (lower bands), 2 = cheeks (blush)
struct Layer {
  uint32_t rgb, t0, until;   // until = 0: no expiry
  uint8_t  fx, pct, lo;
  uint16_t period;
  bool     on;
};
static Layer cloudL[3], over[3];
static volatile uint8_t listenPct = 0;
static volatile uint8_t capPct = 60;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

static uint8_t slotOfTarget(uint8_t t) { return t == LED_EARS ? 0 : t == LED_BELLY ? 1 : 2; }

static void setLayer(Layer& L, uint32_t rgb, uint8_t fx, uint8_t pct, uint32_t ms, uint16_t period, uint8_t lo) {
  uint32_t now = millis();
  L.rgb = rgb; L.fx = fx; L.pct = pct; L.lo = lo; L.t0 = now;
  L.until = ms ? now + ms : 0;
  // photosensitivity (§8): nothing breathes faster than 2 Hz
  L.period = period ? max<uint16_t>(period, 500) : 0;
  L.on = true;
}

void glowCloud(uint8_t target, uint32_t rgb, uint8_t effect, uint8_t brightness) {
  static const uint8_t FX[] = {GFX_SOLID, GFX_PULSE, GFX_RAINBOW};
  portENTER_CRITICAL(&mux);
  setLayer(cloudL[slotOfTarget(target)], rgb, FX[effect % 3], brightness, 0, 0, 0);
  portEXIT_CRITICAL(&mux);
}

void glowSet(GlowRegion r, uint32_t rgb, GlowFx fx, uint8_t pct, uint32_t ms, uint16_t periodMs, uint8_t lo) {
  portENTER_CRITICAL(&mux);
  if (r == GR_SIDE) {
    setLayer(over[0], rgb, fx, pct, ms, periodMs, lo);
    setLayer(over[1], rgb, fx, pct, ms, periodMs, lo);
  } else {
    setLayer(over[r == GR_EARS ? 0 : r == GR_BELLY ? 1 : 2], rgb, fx, pct, ms, periodMs, lo);
  }
  portEXIT_CRITICAL(&mux);
}

void glowClear(GlowRegion r) {
  portENTER_CRITICAL(&mux);
  if (r == GR_SIDE) { over[0].on = over[1].on = false; }
  else over[r == GR_EARS ? 0 : r == GR_BELLY ? 1 : 2].on = false;
  portEXIT_CRITICAL(&mux);
}

void glowClearAll() {
  portENTER_CRITICAL(&mux);
  for (Layer& L : over) L.on = false;
  portEXIT_CRITICAL(&mux);
}

void glowListening(uint8_t pct) { listenPct = pct; }
void glowCap(uint8_t pct) { capPct = pct; }

static uint32_t hue(uint32_t deg) {   // full saturation and value
  uint8_t sector = deg / 60, f = (deg % 60) * 255 / 60;
  switch (sector) {
    case 0:  return 0xFF0000 | f << 8;
    case 1:  return (255 - f) << 16 | 0x00FF00;
    case 2:  return 0x00FF00 | f;
    case 3:  return (255 - f) << 8 | 0x0000FF;
    case 4:  return f << 16 | 0x0000FF;
    default: return 0xFF0000 | (255 - f);
  }
}

static uint32_t scaleRgb(uint32_t rgb, float k) {
  if (k <= 0) return 0;
  if (k > 1) k = 1;
  return (uint32_t)(((rgb >> 16) & 0xFF) * k) << 16 | (uint32_t)(((rgb >> 8) & 0xFF) * k) << 8 |
         (uint32_t)((rgb & 0xFF) * k);
}

// Colour of one layer at `now`, already multiplied by its brightness; 0 when off.
static uint32_t evalLayer(Layer& L, uint32_t now, bool cloud) {
  if (!L.on) return 0;
  if (L.until && (int32_t)(now - L.until) >= 0) { L.on = false; return 0; }
  float t = (float)(now - L.t0), f = 1.0f;
  uint32_t rgb = L.rgb;
  switch (L.fx) {
    case GFX_PULSE:     f = 0.8f + 0.2f * cosf(TWO_PI * t / 2000.0f); break;   // 2 s, depth 40 %, never off
    case GFX_RAINBOW:   rgb = hue(((uint32_t)t / 1000 * 60 + ((uint32_t)t % 1000) * 60 / 1000) % 360); break;  // 6 s cycle
    case GFX_HEARTBEAT: f = 0.25f + 0.75f * (0.5f - 0.5f * cosf(TWO_PI * t / 1000.0f)); break;
    case GFX_BREATHE: {
      float w = 0.5f - 0.5f * cosf(TWO_PI * t / (L.period ? L.period : 8000));
      f = L.pct ? (L.lo + (L.pct - L.lo) * w) / L.pct : 0;
      break;
    }
    case GFX_SHIMMER:   f = 0.65f + 0.2f * sinf(TWO_PI * 1.1f * t / 1000) + 0.15f * sinf(TWO_PI * 0.6f * t / 1000 + 1); break;
    case GFX_SWELL:     f = L.until ? sinf(PI * t / (float)(L.until - L.t0)) : 1; break;
    case GFX_FADE:      f = L.until ? 1.0f - t / (float)(L.until - L.t0) : 1; break;
    default: break;
  }
  // led.brightness is multiplied by the cap; behaviour glows are clamped to it (§8)
  float pct = cloud ? L.pct * capPct / 100.0f : min<float>(L.pct, capPct);
  return scaleRgb(rgb, f * pct / 100.0f);
}

GlowFrame glowEval(uint32_t now) {
  GlowFrame g;
  uint32_t slot[3];
  portENTER_CRITICAL(&mux);
  for (int i = 0; i < 3; i++) {
    uint32_t o = evalLayer(over[i], now, false);
    slot[i] = over[i].on ? o : evalLayer(cloudL[i], now, true);
  }
  portEXIT_CRITICAL(&mux);
  // the privacy indicator beats everything, caps included (§5.9: nothing overrides it)
  uint8_t lp = listenPct;
  if (lp) slot[0] = slot[1] = scaleRgb(RGB_LISTEN, lp / 100.0f);
  g.upper = slot[0]; g.lower = slot[1]; g.cheeks = slot[2];
  return g;
}
