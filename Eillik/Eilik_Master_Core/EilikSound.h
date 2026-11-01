// Eilik voice: 38 synthesized bleep-bloop effects, one per facial expression
// (designs per Eilik_Sound_Generation_Prompt.md). All sounds are generated
// mathematically (sine/square sweeps, arpeggios, vibrato, noise) — no files.
//
// Non-blocking: synthesis runs in a FreeRTOS task pinned to core 0 and
// streams to the MAX98357A over I2S; the display loop on core 1 never waits.
#ifndef EILIK_SOUND_H
#define EILIK_SOUND_H

#include <Arduino.h>
#include "EilikFace.h"   // sounds are keyed to FaceExpr, 1:1

// WAV clips stored in flash (EilikClipsData.h) — the "cherry on top" voices.
enum ClipId : uint8_t {
  CLIP_BAHHH,    // grumpy protest
  CLIP_HEHE,     // sneaky giggle
  CLIP_HELLO,    // greeting (played at boot)
  CLIP_OI,       // startled "oi!"
  CLIP_PIGYAWN,  // big yawn
  CLIP_RISA,     // hearty laugh
  CLIP_SOARYN,   // excited celebration
  CLIP_SUS,      // suspicious sting
  CLIP_WHAT,     // confused "what?"
  CLIP_ARGHH,    // furious scream
  CLIP_COUNT
};

void soundBegin();                  // start I2S + audio task (call once in setup)
void soundPlayFace(FaceExpr f);     // matching sound for a face; mapped faces play
                                    // their WAV clip ~50% of the time for variety
void soundPlayClip(ClipId c);       // play a specific WAV clip
void soundPlayBoot();               // greeting clip

#endif
