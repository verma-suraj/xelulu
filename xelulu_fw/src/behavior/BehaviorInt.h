// Internal to behavior/: shared by the engine (Behavior.cpp) and the
// reaction tables (Reactions.cpp). loopTask only.
#pragma once
#include "Behavior.h"
#include "../face/Glow.h"
#include "../audio/Sfx.h"
#include "../sense/Sense.h"
#include "../body/Body.h"
#include "../audio/Turn.h"

#define SND_FACE 0xFE                 // the face's own sound, with the 50 % clip voice
#define NO_MOTION 0xFFFF

struct GlowSpec {
  uint8_t region;                     // GlowRegion, 0xFF = none
  uint32_t rgb;
  uint8_t fx, pct;
  uint16_t ms, period;
  uint8_t lo;
  const char* name;                   // for the [react] line
};
extern const GlowSpec NOGLOW;

// One reaction = face + motion + sound/phrase + glow, logged as one line (§14.5).
// Face lands at +100 ms (head first, §5.11.4); motion is local priority (P4).
void react(const char* in, FaceId face, uint16_t motion, uint8_t snd, const char* phrase = nullptr,
           const GlowSpec& glow = NOGLOW, uint8_t motionFlags = 0, uint32_t holdMs = 0, int16_t aim = 0);
void later(uint32_t ms, FaceId f, const char* phrase);   // one follow-up reaction
void moodEnd();                       // back to the base face now
void markChild(uint32_t now);         // a child did something: idle timers reset, wake if asleep
bool motionAllowed();                 // quiet hours, low battery and sleep stop the servos
bool isOnline();
void listeningStart(uint8_t zone, uint32_t now);
void listeningEnd(uint32_t now);

void reactSense(const SenseEvent& e, uint32_t now);
void reactBody(const BodyEvent& e, uint32_t now);
void reactionsTick(uint32_t now);
