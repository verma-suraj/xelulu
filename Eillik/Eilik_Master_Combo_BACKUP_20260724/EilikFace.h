// Face engine for the 240x240 round GC9A01.
// 38 expressions from "Animation sheet.png", animated per "Animations.md":
//  - global breathing bob on every face (nothing is ever still)
//  - organic randomized blinks (fast close, slower open, tiny bounce)
//  - squash & stretch on blinks
//  - per-face scripted animation (see each entry in Animations.md)
#ifndef EILIK_FACE_H
#define EILIK_FACE_H

#include <Arduino.h>
#include <TFT_eSPI.h>

enum FaceExpr : uint8_t {
  // Row 1: Joy & Excitement
  FACE_SMILE,          //  1 standard smile
  FACE_GIDDY,          //  2 bouncing high arcs
  FACE_ECSTATIC,       //  3 heartbeat pulse + particles
  FACE_CHARMED,        //  4 round eyes + pulsing blush
  FACE_SPARKLE,        //  5 happy arches + rotating stars
  FACE_WINK,           //  6 playful wink
  FACE_STARRY,         //  7 star eyes morphing to circles
  FACE_LAUGH,          //  8 chuckling crescents
  FACE_EAGER,          //  9 darting round eyes + double blink
  FACE_ANTICIPATION,   // 10 low eyes sliding up
  // Row 2: Sadness & Anger
  FACE_POUT,           // 11 flat-top circles drifting apart
  FACE_TEARY,          // 12 watery eyes, sloshing
  FACE_CRYING,         // 13 downturned arcs + falling tears
  FACE_DISAPPOINTED,   // 14 low domes, long blink, looks away
  FACE_HEARTBROKEN,    // 15 cracked heart, pulsing crack
  FACE_GRUMPY,         // 16 brow-cut circles, darting
  FACE_FURIOUS,        // 17 red slanted eyes + flame flicker
  FACE_FUMING,         // 18 red slits + steam puffs
  FACE_ANNOYED,        // 19 half-lidded twitch + eye roll
  FACE_FLUSHED,        // 20 overheated orange, erratic blinks
  // Row 3: Curiosity, Playful & Affection
  FACE_QUESTIONING,    // 21 one raised arched brow-eye
  FACE_INVESTIGATING,  // 22 magnifying-glass eye scanning
  FACE_THOUGHTFUL,     // 23 eyes drifting in a figure-eight
  FACE_CONFUSED,       // 24 counter-rotating spirals
  FACE_WONDER,         // 25 massive eyes + drifting glints
  FACE_PLAYFUL,        // 26 rolling wave arcs
  FACE_MISCHIEVOUS,    // 27 sly slide to the edge and snap back
  FACE_CHEEKY,         // 28 squints + wiggling pink tongue
  FACE_DERP,           // 29 independent goofy eyes
  FACE_LOVING,         // 30 beating hearts
  // Row 4: Fatigue & Reactive
  FACE_TIRED,          // 31 drooping lids that snap open
  FACE_SLEEPING,       // 32 flat lines + floating Zzz
  FACE_YAWNING,        // 33 squints + big trembling yawn
  FACE_SLEEPY,         // 34 wobbly fading eyes
  FACE_SCARED,         // 35 small jittering eyes darting to corners
  FACE_BORED,          // 36 pill lines, lethargic blink, sighs
  FACE_EMBARRASSED,    // 37 eyes dart away + spreading blush
  FACE_HUNGRY,         // 38 pleading eyes tracking a floating fish
  FACE_COUNT
};

void faceBegin(TFT_eSprite* sprite);  // sprite must already be created 240x240
void faceSet(FaceExpr f);
void faceUpdate();                    // call every loop(); renders + pushes sprite
void faceLabel(const char* name);     // optional on-screen caption (nullptr = off)

#endif
