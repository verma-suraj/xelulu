// Keyframe tables for every movement (FW-SOT §7.2-7.6), as data.
// A motion is a list of segments over time (ms from its start); the body's
// player evaluates them at 50 Hz, adds the layers, clamps, and rate-limits.
#pragma once
#include <Arduino.h>
#include "../face/Face.h"

// joints masks
#define J_HP   1
#define J_AL   2
#define J_AR   4
#define J_ARMS 6
#define J_ALL  7

enum KfOp : uint8_t {
  KF_MV,     // move masked joints to (a=HP, b=AL, c=AR) between t0 and t1
  KF_OSC,    // masked joints alternate a -> b -> a ... , c ms per half
  KF_TRM,    // additive tremor: a deg, b = Hz x 10, c = 1 decays to 0 by t1
  KF_EYE,    // at t0: eye gaze a, b px; eye-height tilt c px (no head tilt on this build)
  KF_SND,    // at t0: sound a (skipped while Xelulu speaks)
  KF_WAIT5,  // high_five: hold t0..t1 waiting for the five, then chain `celebrate`
  KF_LOOP,   // at t0: jump back to time a (looping "while ..." motions)
  KF_TORQ,   // at t0: torque profile a (TP_*) on mask
};
// ease (low 3 bits) + flags
enum Ease : uint8_t { E_LIN, E_EIO, E_EO, E_SPR, E_HOLD };
#define E_ANTI 0x10   // OSC: the right arm runs in anti-phase
#define E_AIM  0x20   // MV: HP target = aim + a, clamped to +-b
#define E_REL  0x40   // OSC: a and b are offsets from where the segment starts
enum TorqueProfile : uint8_t { TP_NORMAL, TP_GENTLE, TP_LIMP };

struct Kf {
  uint16_t t0, t1;
  uint8_t  op, mask, ease;
  int16_t  a, b, c;
};
struct Motion { const Kf* kf; uint8_t n; uint16_t durMs; const char* name; };   // durMs 0xFFFF = loops

// Gestures (§7.3): Document B's names first, then the firmware vocabulary (G14)
enum GestureId : uint8_t {
  G_WAVE, G_HIGH_FIVE, G_HUG, G_CLAP, G_POINT_LEFT, G_NOD, G_SHAKE,
  G_WAVE_LEFT, G_HIGH_FIVE_LEFT, G_POINT_RIGHT, G_CELEBRATE, G_SHRUG, G_THINK, G_SCARED,
  G_STRETCH, G_TILT_CURIOUS, G_LOOK_UP, G_LOOK_DOWN, G_LOOK_LEFT, G_LOOK_RIGHT, G_BOW,
  G_COUNT
};
// Local reaction motions (§7.5) that are not a face's default motion
enum LocalMotion : uint8_t {
  L_HI_PERK = 0,      // head tap: HP +-8 wiggle, arms perk 10 -> 22 -> 10, eyes up 8
  L_HEART,            // belly tap: hand on heart, arms 10 -> 50 -> 10 slow, eyes down 4
  L_PURR,             // head pat: HP sway +-5 at 0.6 Hz, arms relax (loops while patting)
  L_SHIMMY,           // belly pat: tickle shimmy
  L_GLANCE_BACK,      // back tap, nothing there: glance over the shoulder
  L_TURN_BACK,        // back tap, someone there, no wheels: HP -> +60 over 400 ms
  L_ARM_SHIMMY,       // back pat without wheels: arm shimmy + HP +-12
  L_HAND_HOLD,        // hand held: head looks at it, eyes down 4 (side by mirror flag)
  L_PICKUP,           // picked up: arms -> 40
  L_FALL,             // falling: arms in
  L_HUG_LIFTED,       // lifted + held: HUG pose (gentle)
  L_GLANCE,           // idle glance: eyes + HP +-10 (side by mirror flag)
  L_APPROACH,         // someone approaching: head pans toward that side
  L_DIZZY,            // spin: head wobble +-15 at 3 Hz, decaying
  L_SCAN,             // carried: head scans
  L_CLAPBACK,         // clapping game: one clap (repeated by the caller)
  L_COUNT
};
// motion id space: [0, G_COUNT) gestures, then faces, then local motions
#define MOT_FACE(f)  ((uint16_t)G_COUNT + (uint16_t)(f))
#define MOT_LOCAL(l) ((uint16_t)G_COUNT + (uint16_t)FACE_COUNT + (uint16_t)(l))
#define MOT_COUNT    ((uint16_t)G_COUNT + (uint16_t)FACE_COUNT + (uint16_t)L_COUNT)
#define MOT_DANCE    0xFFFE      // generated, see motionDance()

extern const char* const GESTURE_NAMES[G_COUNT];
int gestureFromName(const char* name);   // -1 unknown
const Motion* motionGet(uint16_t id);    // nullptr = no motion (SMILE, CHARGING)
bool motionMirrored(uint16_t id);        // wave_left, high_five_left, point_right, look_right

// Dances (§7.6): 10 patterns, written per beat; generated into `out` (<= max).
// Moves that exceed a speed cap lose amplitude, never the beat.
uint8_t motionDance(uint8_t pattern, uint16_t bpm, uint8_t bars, Kf* out, uint8_t max);
