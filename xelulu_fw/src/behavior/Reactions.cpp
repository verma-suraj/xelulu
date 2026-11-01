// FW-SOT §7.5 as code: every input -> face, motion, sound, glow, and the wire
// event. Deliberate changes from Eillik (§7.5.1): no anger escalation from a
// child's touches, and a pickup is "wheee", not "scared".
#include "BehaviorInt.h"
#include "../State.h"
#include "../store/Store.h"
#include "../net/Net.h"
#include "../body/Safety.h"
#include "../../Contract.h"

static uint32_t downAt[ZONES];
static uint32_t comboUntil = 0, gazeBackAt = 0;
static uint8_t patBursts = 0;
static uint32_t patWindow = 0, purrUntil = 0;
static uint32_t choiceUntil = 0;              // belly double tap: story or song?
static bool listening = false, hugging = false, calm = false;
static uint8_t listenZone = 0;
static uint32_t calmEndAt = 0;
static int8_t handHold = -1;                  // zone of the held hand, -1 none
static uint32_t highFiveUntil[2] = {0, 0};
static uint8_t clapTaps[2] = {0, 0};
static bool clapGame[2] = {false, false};
static uint32_t lastKnock = 0, roughFirst = 0, roughAt = 0;
static uint8_t roughCount = 0;
static uint8_t shakes = 0;
static uint32_t lastShake = 0, shakeQuietUntil = 0, lastApproach = 0;
static bool backTurning = false;
static uint32_t grabEndAt = 0;
// one follow-up beat: "QUESTIONING -> then GIDDY + found_you"
static uint32_t followAt = 0;
static FaceId followFace;
static const char* followPhrase = nullptr;

void later(uint32_t ms, FaceId f, const char* phrase) {
  followAt = millis() + ms; followFace = f; followPhrase = phrase;
}

static const GlowSpec GL_HEART = {GR_BELLY, RGB_HEART, GFX_HEARTBEAT, 60, 3000, 0, 0, "belly:heartbeat"};
static const GlowSpec GL_STORY = {GR_BELLY, RGB_AMBER, GFX_SOLID, 40, 6000, 0, 0, "belly:amber 40%"};
static const GlowSpec GL_SPARK = {GR_BELLY, RGB_PINK, GFX_SHIMMER, 50, 3000, 0, 0, "belly:pink sparkle"};
static const GlowSpec GL_PURR  = {GR_SIDE, RGB_WARM, GFX_SOLID, 30, 6000, 0, 0, "side:warm 30%"};
static const GlowSpec GL_HUG   = {GR_BELLY, RGB_PINK, GFX_BREATHE, 50, 0, 2000, 15, "belly:pink pulse 0.5Hz"};
static const GlowSpec GL_CALM  = {GR_BELLY, RGB_TEAL, GFX_BREATHE, 60, 0, 8000, 10, "belly:teal breathing"};
static const GlowSpec GL_POP   = {GR_SIDE, RGB_WARM, GFX_SWELL, 50, 500, 0, 0, "side:warm pop"};
static const GlowSpec GL_COMFORT = {GR_SIDE, RGB_AMBER, GFX_BREATHE, 40, 0, 4000, 10, "side:amber pulse 0.25Hz"};

// ---------------------------------------------------------------------------
// listening = push-to-talk (§5.3). The privacy indicator is on for all of it.
// ---------------------------------------------------------------------------
void listeningStart(uint8_t zone, uint32_t now) {
  listening = true;
  listenZone = zone;
  netEventWake(WAKE_TOUCH, -1);
  if (zone == Z_BELLY) netEventButton(BTN_PRESS);
  glowListening(60);                          // LISTENING face + blue side glow, always together (§5.9)
  react(zone == Z_BELLY ? "touch:belly:hold" : "touch:head:hold", FACE_LISTENING, MOT_FACE(FACE_LISTENING),
        S_LISTEN, nullptr, NOGLOW, 0, 600000, 0);
  // online: the upload starts at the hold threshold (§5.3). ponytail: offline copycat (§7.8) is not built yet
  if (isOnline()) turnStart(true);
}

void listeningEnd(uint32_t now) {
  if (!listening) return;
  listening = false;
  if (listenZone == Z_BELLY) netEventButton(BTN_RELEASE);
  bodyStop(P_LOCAL);
  if (turnState() != T_IDLE) { turnRelease(); return; }   // the turn's events drive the face from here
  glowListening(0);
  if (!isOnline())   // §7.5.3: wake while offline -> OFFLINE SORRY -> GIDDY (the wake is queued)
    react("wake:offline", FACE_OFFLINE_SORRY, MOT_FACE(FACE_OFFLINE_SORRY), S_NONE, "star_library_sleeping", NOGLOW, 0, 4000);
  else moodEnd();
}

static void roughHit(uint32_t now) {          // XL-T06: touch + knock spike within 100 ms, x3 in 5 s
  if (now - roughFirst > 5000) { roughFirst = now; roughCount = 0; }
  if (++roughCount >= 3) {
    roughCount = 0;
    roughAt = now;
    react("touch:rough_play", FACE_TEARY, MOT_FACE(FACE_CRYING), S_NONE, "ouch_gentle", NOGLOW, 0, 3000);
  }
}

static void handGesture(uint8_t z, uint8_t g, uint32_t now) {
  uint8_t side = z == Z_LHAND ? 0 : 1;
  bool left = side == 0;
  switch (g) {
    case TG_TAP:
      if (now < highFiveUntil[side]) { bodyFive(); highFiveUntil[side] = 0; return; }   // the five!
      if (clapGame[side]) { clapTaps[side]++; return; }
      react(left ? "touch:left_hand:tap" : "touch:right_hand:tap", FACE_GIDDY, left ? G_WAVE_LEFT : G_WAVE, FACE_WINK);
      break;
    case TG_DOUBLE:
      react(left ? "touch:left_hand:double_tap" : "touch:right_hand:double_tap", FACE_ECSTATIC,
            left ? G_HIGH_FIVE_LEFT : G_HIGH_FIVE, SND_FACE, nullptr, GL_POP);
      highFiveUntil[side] = now + 3500;
      break;
    case TG_HOLD:   // "where are we going?": the arm goes compliant, the head looks at it
      handHold = z;
      bodyCompliant(left ? AL : AR, 60000);
      react(left ? "touch:left_hand:hold" : "touch:right_hand:hold", FACE_LOVING, MOT_LOCAL(L_HAND_HOLD), S_NONE,
            "where_are_we_going", NOGLOW, left ? 0 : BF_MIRROR, 600000);
      break;
    case TG_PAT:    // clapping game: clap back the same count when the child stops
      clapGame[side] = true;
      clapTaps[side] = 3;
      react(left ? "touch:left_hand:pat" : "touch:right_hand:pat", FACE_PLAYFUL, NO_MOTION, S_NONE);
      break;
  }
}

static void touchGesture(uint8_t z, uint8_t g, uint32_t now) {
  netEventTouch(ZONES_FITTED[z], g);          // every classified touch goes on the wire (§7.5)
  markChild(now);
  if (now < comboUntil && (z == Z_HEAD || z == Z_BELLY)) return;   // the combo already answered
  if (g == TG_TAP && (z == Z_HEAD || z == Z_BELLY) && (turnState() == T_PLAYING || turnState() == T_DRAINING)) {
    turnBargeIn();                            // a tap over the reply is a barge-in (§5.5)
    return;
  }

  // story-or-song window after a belly double tap (offline path; online it is the follow-up window)
  if (now < choiceUntil && g == TG_TAP && (z == Z_HEAD || z == Z_BACK)) {
    choiceUntil = 0;
    if (z == Z_HEAD) {
      // ponytail: cached stories need the SD player; say so kindly instead
      react("choice:story", FACE_OFFLINE_SORRY, MOT_FACE(FACE_OFFLINE_SORRY), S_NONE, "star_library_sleeping", NOGLOW, 0, 4000);
    } else {
      react("choice:song", FACE_STARRY, NO_MOTION, SND_FACE, nullptr, {GR_SIDE, 0, GFX_RAINBOW, 40, 20000, 0, 0, "side:rainbow"});
      if (motionAllowed()) bodyDance(random(1, 11), 100, 8, P_LOCAL);
    }
    return;
  }
  if (z >= Z_LHAND) { handGesture(z, g, now); return; }

  if (z == Z_HEAD) {
    switch (g) {
      case TG_TAP:    react("touch:head:tap", FACE_WINK, MOT_LOCAL(L_HI_PERK), S_NONE, "hi"); break;
      case TG_DOUBLE: react("touch:head:double_tap", FACE_PLAYFUL, MOT_FACE(FACE_PLAYFUL), SND_FACE); break;
      case TG_HOLD:   listeningStart(Z_HEAD, now); break;
      case TG_PAT:
        if (now - patWindow > 10000) { patWindow = now; patBursts = 0; }
        if (++patBursts >= 4) {               // over-pat: 4 bursts in 10 s
          patBursts = 0;
          react("touch:head:over_pat", FACE_EMBARRASSED, MOT_FACE(FACE_EMBARRASSED), S_NONE, "hehe_that_tickles");
        } else {
          react("touch:head:pat", FACE_SPARKLE, MOT_LOCAL(L_PURR), S_PURR, nullptr, GL_PURR);
          if (!S.turnOpen) sfxPlay(S_PURR, 0, 1);   // purr while patting, max 6 s
          purrUntil = now + 6000;
        }
        if (now - roughAt < 10000) {           // a gentle pat after rough play
          roughAt = 0;
          react("touch:pat_after_rough", FACE_LOVING, MOT_FACE(FACE_LOVING), S_NONE, "thank_you_soft");
        }
        break;
    }
  } else if (z == Z_BELLY) {
    switch (g) {
      case TG_TAP:
        react("touch:belly:tap", FACE_CHARMED, MOT_LOCAL(L_HEART), FACE_LOVING, "thats_my_heart", GL_HEART, 0, 4000);
        break;
      case TG_DOUBLE:
        react("touch:belly:double_tap", FACE_EAGER, MOT_FACE(FACE_EAGER), S_NONE, "story_or_song", GL_STORY);
        choiceUntil = now + 6000;
        break;
      case TG_HOLD:
        if (S.lifted) {                        // held in the air: a hug, not talk (XL-T03)
          hugging = true;
          react("touch:belly:hold+lifted", FACE_LOVING, MOT_LOCAL(L_HUG_LIFTED), S_PURR, nullptr, GL_HUG, 0, 600000);
        } else listeningStart(Z_BELLY, now);
        break;
      case TG_PAT:
        react("touch:belly:pat", FACE_LAUGH, MOT_LOCAL(L_SHIMMY), SND_FACE, nullptr, GL_SPARK);
        break;
    }
  } else if (z == Z_BACK) {
    switch (g) {
      case TG_TAP: {   // "Who's there?"
        uint16_t rear = S.tofMm[TOF_REAR];
        bool someone = rear != 0xFFFF && rear < 800;
        if (someone && wheelsAllowed()) {
          react("touch:back:tap+someone", FACE_QUESTIONING, NO_MOTION, SND_FACE);
          bodyDrive(DM_TURN, 0, 2000, (uint16_t)(PI * cal.wheelBaseMm / 2), false, 0);   // in place, 180 deg
          backTurning = true;
        } else if (someone) {
          react("touch:back:tap+someone", FACE_QUESTIONING, MOT_LOCAL(L_TURN_BACK), SND_FACE);
          later(600, FACE_GIDDY, "found_you");
        } else {
          react("touch:back:tap", FACE_QUESTIONING, MOT_LOCAL(L_GLANCE_BACK), S_NONE, "whos_there");
        }
        break;
      }
      case TG_DOUBLE:   // spin dance
        react("touch:back:double_tap", FACE_STARRY, NO_MOTION, SND_FACE);
        if (wheelsAllowed()) {
          bodyDrive(DM_TURN, 0, 3000, (uint16_t)(TWO_PI * cal.wheelBaseMm / 2), false, 0);   // 360 deg at 3 rad/s
          if (motionAllowed()) bodyMotion(MOT_FACE(FACE_STARRY), P_LOCAL);
        } else if (motionAllowed()) bodyDance(10, 100, 2, P_LOCAL);   // HP sweep + dance_9's arms
        break;
      case TG_HOLD:     // calm mode: breathe while held + 2 cycles after (XL-L08)
        calm = true;
        calmEndAt = 0;
        react("touch:back:hold", FACE_BREATHE, MOT_FACE(FACE_BREATHE), S_BREATHE, "lets_breathe", GL_CALM, 0, 600000);
        break;
      case TG_PAT:      // tickle: wiggles away a little
        react("touch:back:pat", FACE_LAUGH, wheelsAllowed() ? NO_MOTION : MOT_LOCAL(L_ARM_SHIMMY), SND_FACE);
        if (wheelsAllowed()) bodyDrive(DM_LINEAR, 100, 0, 30, false, 0);
        later(1200, FACE_GIDDY, nullptr);
        break;
    }
  }
}

static void touchDown(uint8_t z, uint32_t now) {
  markChild(now);                             // a touch wakes it: SMILE, then the touch's own reaction
  downAt[z] = now;
  if (now - lastKnock < 100) roughHit(now);
  if ((z == Z_HEAD && now - downAt[Z_BELLY] < 300) || (z == Z_BELLY && now - downAt[Z_HEAD] < 300)) {
    comboUntil = now + 1500;                  // head + belly together: ECSTATIC, talk suppressed
    react("touch:head+belly", FACE_ECSTATIC, MOT_FACE(FACE_ECSTATIC), SND_FACE, nullptr, GL_POP);
    return;
  }
  if (clapGame[0] && z == Z_LHAND) clapTaps[0]++;
  if (clapGame[1] && z == Z_RHAND) clapTaps[1]++;
  // micro-reaction at 0 ms: the eyes glance toward the zone, one-frame squash, no sound
  static const int8_t GX[ZONES] = {0, 14, 0, 18, -18}, GY[ZONES] = {-8, 0, 8, 6, 6};
  faceSquash();
  faceGaze(GX[z], GY[z]);
  gazeBackAt = now + 400;
}

static void touchUp(uint8_t z, bool wasHold, uint32_t now) {
  if (listening && z == listenZone) listeningEnd(now);
  if (hugging && (z == Z_BELLY || z == Z_BACK)) { hugging = false; bodyStop(P_LOCAL); moodEnd(); }
  if (calm && z == Z_BACK) calmEndAt = now + 16000;   // two more 8 s breaths
  if (handHold == z) {                        // the arm comes back to REST, gently
    handHold = -1;
    bodyCompliant(z == Z_LHAND ? AL : AR, 0);
    bodyStop(P_LOCAL);
    moodEnd();
  }
}

// ---------------------------------------------------------------------------
void reactSense(const SenseEvent& e, uint32_t now) {
  switch (e.kind) {
    case SE_TOUCH_DOWN: touchDown(e.a, now); break;
    case SE_TOUCH:      touchGesture(e.a, e.b, now); break;
    case SE_TOUCH_UP:   touchUp(e.a, e.b, now); break;
    case SE_PAT_END:
      if (e.a == Z_HEAD && purrUntil) { purrUntil = 0; sfxStop(); bodyStop(P_LOCAL); }
      if (e.a >= Z_LHAND) {
        uint8_t side = e.a == Z_LHAND ? 0 : 1;
        if (clapGame[side]) {                 // clap back the same count (max 8)
          clapGame[side] = false;
          uint8_t n = min<uint8_t>(clapTaps[side], 8);
          Serial.printf("[react] in=clap_game  claps back %u\n", n);
          if (motionAllowed()) bodyClaps(n, P_LOCAL);
        }
      }
      break;

    case SE_MOTION:
      netEventMotion(e.a, e.value);           // the wire event, always
      markChild(now);
      switch (e.a) {
        case WM_PICKUP:
          react("motion:pickup", FACE_EAGER, MOT_LOCAL(L_PICKUP), S_NONE, "up_we_go", NOGLOW, 0, 600000);
          break;
        case WM_SHAKE:
          if (now < shakeQuietUntil) break;
          if (now - lastShake > 12000) shakes = 0;
          lastShake = now;
          if (++shakes == 1) {
            react("motion:shake", FACE_CONFUSED, MOT_FACE(FACE_CONFUSED), SND_FACE);
            later(2500, FACE_GIDDY, nullptr);
          } else if (shakes == 2) {
            react("motion:shake x2", FACE_CONFUSED, MOT_FACE(FACE_UPSIDE_DOWN), FACE_CONFUSED, nullptr, NOGLOW, 0, 4000);
            later(2500, FACE_CONFUSED, nullptr);
          } else {
            react("motion:shake x3", FACE_CONFUSED, MOT_FACE(FACE_DERP), S_NONE, "my_head_is_spinning");
            later(1500, FACE_DERP, nullptr);
            shakes = 0;
            shakeQuietUntil = now + 5000;     // then 5 s with no shake reactions
          }
          break;
        case WM_UPSIDE_DOWN:
          faceFlip(true);                     // setRotation(3): free
          react("motion:upside_down", FACE_UPSIDE_DOWN, MOT_FACE(FACE_UPSIDE_DOWN), S_NONE, "world_upside_down", NOGLOW, 0, 600000);
          break;
        case WM_TILT:
          if (S.lifted) react("motion:tilt+lifted", FACE_WONDER, MOT_FACE(FACE_WONDER), SND_FACE);
          else react("motion:tilt", FACE_EMBARRASSED, MOT_FACE(FACE_EMBARRASSED), S_NONE, "help_me_up");
          break;
        case WM_DROP: break;                  // reported at the end of the fall; SCARED already showed
      }
      break;
    case SE_FALLING:
      react("motion:falling", FACE_SCARED, MOT_LOCAL(L_FALL), S_NONE, nullptr, NOGLOW, 0, 600000);
      break;
    case SE_IMPACT:
      bodyStop(P_LOCAL);
      react("motion:impact", FACE_TEARY, NO_MOTION, S_NONE, "oops_im_okay", NOGLOW, 0, 1500);
      break;
    case SE_PUT_DOWN:
      bodyStop(P_LOCAL);
      if (hugging) hugging = false;
      react("motion:put_down", FACE_GIDDY, NO_MOTION, SND_FACE);
      break;
    case SE_UPRIGHT:
      faceFlip(false);
      bodyStop(P_LOCAL);
      moodEnd();
      break;
    case SE_TILT_CLEAR: break;
    case SE_SPIN:
      react("motion:spin", FACE_CONFUSED, MOT_LOCAL(L_DIZZY), SND_FACE, nullptr, NOGLOW, 0, 3000);
      break;
    case SE_KNOCK:
      lastKnock = now;
      if (now - downAt[Z_HEAD] < 100 || now - downAt[Z_BELLY] < 100 || now - downAt[Z_BACK] < 100) roughHit(now);
      else if (!bodyBusy(P_IDLE) && !S.asleep && faceCurrent() == FACE_SMILE) {   // idle only
        react("motion:knock", FACE_WONDER, NO_MOTION, SND_FACE);
        faceGaze(0, -8);
        gazeBackAt = now + 1500;
      }
      break;
    case SE_CARRIED:
      react("motion:carried", FACE_INVESTIGATING, MOT_LOCAL(L_SCAN), SND_FACE);
      break;
    case SE_APPROACH:   // someone approaching: look that way + EAGER, at most every 60 s
      if (now - lastApproach < cal.approachS * 1000UL || S.asleep || bodyBusy(P_LOCAL) || faceCurrent() != FACE_SMILE) break;
      lastApproach = now;
      react(e.a ? "tof:approach_rear" : "tof:approach_front", FACE_EAGER,
            e.a ? MOT_LOCAL(L_APPROACH) : MOT_FACE(FACE_EAGER), S_NONE);
      break;
  }
}

void reactBody(const BodyEvent& e, uint32_t now) {
  switch (e.kind) {
    case BE_CLIFF:      // §12: braked at an edge. AT_EDGE for 3 s is the body's.
      react(e.a ? "safety:cliff_rear" : "safety:cliff_front", FACE_SCARED, NO_MOTION, S_EDGE, nullptr, NOGLOW, 0, 3000);
      break;
    case BE_ARM_GRAB:   // no pad needed: the servo's load says a child is holding the arm
      bodyCompliant(e.a, 2000);
      react(e.a == AL ? "arm:grab_left" : "arm:grab_right", FACE_LOVING, MOT_LOCAL(L_HAND_HOLD), S_NONE,
            "where_are_we_going", NOGLOW, e.a == AL ? 0 : BF_MIRROR, 2500);
      grabEndAt = now + 2500;
      break;
    case BE_FIVE:
      react("arm:high_five", FACE_ECSTATIC, NO_MOTION, SND_FACE, nullptr, GL_POP);
      break;
    case BE_DRIVE_DONE:
      if (backTurning && !e.ticket) { backTurning = false; react("touch:back:found", FACE_GIDDY, MOT_FACE(FACE_GIDDY), S_NONE, "found_you"); }
      break;
    default: break;
  }
}

void reactionsTick(uint32_t now) {
  if (gazeBackAt && now > gazeBackAt) { gazeBackAt = 0; faceGaze(0, 0); }
  if (followAt && now > followAt) {
    followAt = 0;
    react("follow", followFace, MOT_FACE(followFace), followPhrase ? S_NONE : SND_FACE, followPhrase);
  }
  if (purrUntil && now > purrUntil) { purrUntil = 0; sfxStop(); }
  if (grabEndAt && now > grabEndAt && handHold < 0) { grabEndAt = 0; bodyStop(P_LOCAL); }
  if (calm && calmEndAt && now > calmEndAt) { calm = false; glowClear(GR_BELLY); bodyStop(P_LOCAL); moodEnd(); }
  if (choiceUntil && now > choiceUntil) {     // nothing chosen in 6 s: a random song + dance
    choiceUntil = 0;
    react("choice:timeout", FACE_STARRY, NO_MOTION, SND_FACE, nullptr, {GR_SIDE, 0, GFX_RAINBOW, 40, 20000, 0, 0, "side:rainbow"});
    if (motionAllowed()) bodyDance(random(1, 11), 100, 8, P_LOCAL);
  }
  // XL-C01 (§7.5.3): crying >= 5 s -> COMFORT until 20 s without crying. The cry event went out
  // from the detector. ponytail: no DOA yet, so no turn toward the sound and no hum
  static bool comforting = false;
  if (S.crying != comforting) {
    comforting = S.crying;
    if (comforting) {
      markChild(now);
      react("cry:5s", FACE_COMFORT, MOT_FACE(FACE_COMFORT), S_NONE, "im_here", GL_COMFORT, 0, 600000);
    } else {
      bodyStop(P_LOCAL);                      // REST, slowly
      react("cry:stopped_20s", FACE_SMILE, NO_MOTION, S_NONE, nullptr,
            {GR_SIDE, RGB_AMBER, GFX_FADE, 40, 3000, 0, 0, "side:amber fade 3s"}, 0, 3000);
    }
  }
}
