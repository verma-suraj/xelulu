// Face engine: Eillik's 38 faces unchanged in look, plus Xelulu's 39-48
// (FW-SOT §9). 240x240 canvas in an 8-bit sprite, centred at x = 40 on the
// 320x240 landscape panel; the two 40 px side bands carry the glows (§8).
// Runs on loopTask only, except the setters marked thread-safe.
#pragma once
#include <Arduino.h>

enum FaceId : uint8_t {
  // Row 1: Joy & Excitement
  FACE_SMILE, FACE_GIDDY, FACE_ECSTATIC, FACE_CHARMED, FACE_SPARKLE,
  FACE_WINK, FACE_STARRY, FACE_LAUGH, FACE_EAGER, FACE_ANTICIPATION,
  // Row 2: Sadness & Anger
  FACE_POUT, FACE_TEARY, FACE_CRYING, FACE_DISAPPOINTED, FACE_HEARTBROKEN,
  FACE_GRUMPY, FACE_FURIOUS, FACE_FUMING, FACE_ANNOYED, FACE_FLUSHED,
  // Row 3: Curiosity, Playful & Affection
  FACE_QUESTIONING, FACE_INVESTIGATING, FACE_THOUGHTFUL, FACE_CONFUSED, FACE_WONDER,
  FACE_PLAYFUL, FACE_MISCHIEVOUS, FACE_CHEEKY, FACE_DERP, FACE_LOVING,
  // Row 4: Fatigue & Reactive
  FACE_TIRED, FACE_SLEEPING, FACE_YAWNING, FACE_SLEEPY, FACE_SCARED,
  FACE_BORED, FACE_EMBARRASSED, FACE_HUNGRY,
  // Xelulu 39-48 (§9.2)
  FACE_LISTENING, FACE_THINKING, FACE_COMFORT, FACE_BREATHE, FACE_STORYTELLER,
  FACE_CHARGING, FACE_LOW_BATTERY, FACE_UPSIDE_DOWN, FACE_OFFLINE_SORRY, FACE_UPDATING,
  FACE_COUNT
};
extern const char* const FACE_NAMES[FACE_COUNT];

void     faceBegin();                    // panel + sprite + DMA, once in setup()
void     faceSet(FaceId f);              // logs "face N NAME" (§9.1)
FaceId   faceCurrent();
void     faceUpdate(uint32_t now);       // render + push at <= 30 fps; skips frames, never catches up
int      faceFromName(const char* name); // "cheeky_tongue" etc. (snake_case of FACE_NAMES), -1 if unknown

// Thread-safe setters (plain volatile writes)
void     faceGaze(int8_t x, int8_t y);   // eye offset target, px; eases over ~150 ms (§9.1)
void     faceEyeTilt(int8_t px);         // one eye higher: the "curious head tilt" (§7.1)
void     faceListenSide(int8_t side);    // LISTENING glints/arcs: -1 robot's right, +1 robot's left
void     faceSpeaking(bool on);          // mouth overlay follows faceEnvelope()
void     faceEnvelope(float e);          // 0..1, from the audio clock (§5.11)
void     faceBlink();
void     faceSquash();                   // one-frame squash: the touch-down micro-reaction
void     faceBrightness(uint8_t pct);    // palette scale; the backlight is hard-wired on
void     faceFlip(bool upsideDown);
void     facePanel(bool on);             // ST7789 display off/on (sleep): black, backlight stays lit
uint16_t faceFps();
class TFT_eSprite;
void     faceOverlay(TFT_eSprite& canvas);   // weak no-op; a test sketch may draw on the canvas before each push
