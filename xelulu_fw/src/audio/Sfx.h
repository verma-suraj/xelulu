// Eillik's voice, as the SFX channel of the mixer (FW-SOT §10). The 38
// synth effects are unchanged; they are rendered on demand by the audio
// task instead of a task of their own, so the AEC reference includes them.
#pragma once
#include <Arduino.h>
#include "../face/Face.h"

// 0..37 = Eillik's set, indexed by FaceId. 38..45 = Xelulu's new effects (§10.2).
enum SoundId : uint8_t {
  S_PURR = 38, S_CLAP, S_LISTEN, S_EDGE, S_POP, S_BREATHE, S_VOL, S_CHARGE,
  SND_COUNT,              // 46: console `sound 1..46`
  S_BOOT = SND_COUNT,     // Eillik's boot jingle (`play bringup_probe`)
  S_NONE = 0xFF,
};
// Minions / meme-soundboard samples, licence unknown: PROTOTYPE ONLY (§10.3)
enum ClipId : uint8_t {
  CLIP_BAHHH, CLIP_HEHE, CLIP_HELLO, CLIP_OI, CLIP_PIGYAWN, CLIP_RISA,
  CLIP_SOARYN, CLIP_SUS, CLIP_WHAT, CLIP_ARGHH, CLIP_COUNT
};
extern const char* const SOUND_NAMES[SND_COUNT + 1];
extern const char* const CLIP_NAMES[CLIP_COUNT];

void sfxPlay(uint8_t id, uint8_t attenDb = 0, uint16_t loopGapMs = 0);  // newest wins
void sfxPlayFace(FaceId f);    // the face's sound; mapped faces use their clip ~50 % of the time
void sfxPlayClip(ClipId c);
void sfxStop();                // cutFade: 8 ms ramp to zero
bool sfxBusy();

// audio task only: the next n mono samples at 16 kHz (zeros when idle)
void sfxRender(int16_t* out, int n);
