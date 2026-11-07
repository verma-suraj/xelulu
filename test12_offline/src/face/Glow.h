// Light effects with no LED hardware: drawn on screen (FW-SOT §8).
// ears = upper halves of both side bands, belly = lower halves,
// cheeks = on-canvas blush. Every modulation is <= 2 Hz (photosensitivity).
#pragma once
#include <Arduino.h>

enum GlowRegion : uint8_t { GR_EARS, GR_BELLY, GR_CHEEKS, GR_SIDE /* ears + belly */ };
enum GlowFx : uint8_t {
  GFX_SOLID, GFX_PULSE, GFX_RAINBOW,   // the three Document B effects
  GFX_HEARTBEAT,                       // one smooth swell per second
  GFX_BREATHE,                         // slow swell between lo and the brightness
  GFX_SHIMMER,                         // smooth "sparkle": two slow sines
  GFX_SWELL,                           // one rise-and-fall over the duration
  GFX_FADE,                            // one fade-out over the duration ("pop")
};

#define RGB_LISTEN  0x2D7DFF   // privacy indicator, never overridden (§5.9)
#define RGB_THINK   0x2EE6E6
#define RGB_AMBER   0xFFB347   // story, comfort
#define RGB_HEART   0xFF4F7B
#define RGB_LOWBAT1 0xFFB36A
#define RGB_LOWBAT2 0xFF8A6A
#define RGB_WARM    0xFFE0B0
#define RGB_PINK    0xFF7FB0
#define RGB_TEAL    0x40E0C0
#define RGB_GREEN   0x40E070
#define RGB_BLUE    0x2D7DFF

// Cloud `led` (persists until replaced). Ear colours wait while the privacy indicator is on.
void glowCloud(uint8_t ledTarget, uint32_t rgb, uint8_t ledEffect, uint8_t brightness);
// Behaviour layer, over the cloud's while it lasts. ms = 0: until glowClear().
// For GFX_BREATHE, period/lo set the cycle (ms) and the floor (%).
void glowSet(GlowRegion r, uint32_t rgb, GlowFx fx, uint8_t pct, uint32_t ms = 0,
             uint16_t periodMs = 0, uint8_t lo = 0);
void glowClear(GlowRegion r);
void glowClearAll();
void glowListening(uint8_t pct);         // 0 = off; else both bands RGB_LISTEN (§5.9)
void glowCap(uint8_t pct);               // 60 normal / 10 quiet / 5 sleep (cal)

struct GlowFrame { uint32_t upper, lower, cheeks; };   // RGB888, already scaled
GlowFrame glowEval(uint32_t now);
