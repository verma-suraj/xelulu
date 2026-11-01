// Behaviour engine core: react(), modes (§6.3), idle life (§6.4), sleep,
// battery reactions (§7.5.5) and cloud command execution (§4.7, §7.5.4).
// The Eillik patterns kept: one `now` per loop, a mood that holds 5-10 s
// then returns to the base face, random fidgets, three random wind-downs.
#include "BehaviorInt.h"
#include "../State.h"
#include "../store/Store.h"
#include "../audio/Audio.h"
#include "../audio/Turn.h"
#include "../net/Net.h"
#include "../body/Safety.h"
#include "../ota/Ota.h"
#include "../../Contract.h"

const GlowSpec NOGLOW = {0xFF, 0, 0, 0, 0, 0, 0, nullptr};

static FaceId baseFace = FACE_SMILE;   // where a mood returns to: SMILE, a looped cloud face, STORYTELLER
static uint32_t baseUntil = 0;         // looped animate: 10 min safety cap
static uint32_t moodUntil = 0;
static FaceId pendingFace = FACE_COUNT;
static uint32_t pendingAt = 0;
static uint32_t lastInteraction = 0, nextGlance = 0, nextFidget = 0;
static uint8_t explicitMode = MODE_IDLE;
static bool quietHours = false, shuttingDown = false;
static uint32_t asleepAt = 0, shutdownAt = 0;
static uint8_t bootStep = 0;

// ---- pending cloud commands: ticket -> cmd_id, for the terminal ack ----
struct Pending { bool used; uint8_t ticket, action; uint32_t doneAt; char cmdId[64]; };
static Pending pend[8];
static uint8_t nextTicket = 1;

static uint8_t addPending(const Cmd& c, uint32_t doneAt = 0) {
  for (Pending& p : pend) {
    if (p.used) continue;
    p = {true, nextTicket, c.action, doneAt, ""};
    strlcpy(p.cmdId, c.cmdId, sizeof p.cmdId);
    if (++nextTicket == 0) nextTicket = 1;
    return p.ticket;
  }
  netAck(c.cmdId, ACK_FAILED);          // eight commands in flight at once: something is wrong
  return 0;
}

static void resolve(uint8_t ticket, bool ok) {
  for (Pending& p : pend)
    if (p.used && p.ticket == ticket) { netAck(p.cmdId, ok ? ACK_DONE : ACK_FAILED); p.used = false; }
}

// ===========================================================================
// react
// ===========================================================================
static const struct { const char* id; uint8_t snd; } PHRASES[] = {   // §10.5 ids -> fallback SFX
  {"hi", S_POP}, {"thats_my_heart", FACE_LOVING}, {"story_or_song", FACE_EAGER}, {"whos_there", FACE_QUESTIONING},
  {"found_you", FACE_GIDDY}, {"where_are_we_going", FACE_CHARMED}, {"up_we_go", FACE_EAGER},
  {"wheee", FACE_ECSTATIC}, {"oops_im_okay", FACE_TEARY}, {"help_me_up", FACE_EMBARRASSED},
  {"world_upside_down", FACE_WONDER}, {"my_head_is_spinning", FACE_CONFUSED}, {"ouch_gentle", FACE_TEARY},
  {"thank_you_soft", FACE_LOVING}, {"hehe_that_tickles", FACE_LAUGH}, {"copy_me", FACE_PLAYFUL},
  {"im_here", FACE_CHARMED}, {"lets_breathe", S_BREATHE}, {"im_hungry", FACE_HUNGRY},
  {"plug_me_in", FACE_TIRED}, {"good_night", FACE_SLEEPY}, {"star_library_sleeping", FACE_POUT},
  {"star_library_slow", FACE_THOUGHTFUL}, {"shh_quiet_time", FACE_SLEEPY},
};

void react(const char* in, FaceId face, uint16_t motion, uint8_t snd, const char* phrase,
           const GlowSpec& g, uint8_t motionFlags, uint32_t holdMs, int16_t aim) {
  uint32_t now = millis();
  if (face < FACE_COUNT) { pendingFace = face; pendingAt = now + 100; }   // head first, face at +100 ms

  const char* mname = "none";
  uint8_t nkf = 0;
  const Motion* m = motion != NO_MOTION ? motionGet(motion) : nullptr;
  if (m && motionAllowed()) {
    bodyMotion(motion, P_LOCAL, motionFlags, 0, aim);   // the body refuses it if P2/P3 own the body
    mname = m->name; nkf = m->n;
  } else if (m) mname = "held (quiet/low battery)";

  char sbuf[48] = "none";
  if (!S.turnOpen) {                                    // no SFX over Xelulu's own voice (§10.2)
    if (snd == SND_FACE && face < FACE_COUNT) {
      sfxPlayFace(face);
      snprintf(sbuf, sizeof sbuf, "%s", face < 38 ? SOUND_NAMES[face] : face == FACE_LISTENING ? "S_LISTEN" : "none");
    } else if (snd <= SND_COUNT) {
      sfxPlay(snd);
      snprintf(sbuf, sizeof sbuf, "%s", SOUND_NAMES[snd]);
    }
    if (phrase) {
      // the phrase pack lives on SD (§10.5); until it is there the matching SFX stands in
      size_t len = strlen(sbuf);
      if (snd == S_NONE) {
        for (auto& p : PHRASES)
          if (!strcmp(p.id, phrase)) { sfxPlay(p.snd); snprintf(sbuf, sizeof sbuf, "%s", SOUND_NAMES[p.snd]); break; }
        len = strlen(sbuf);
      }
      snprintf(sbuf + len, sizeof sbuf - len, "+%s", phrase);
    }
  }
  if (g.region != 0xFF) glowSet((GlowRegion)g.region, g.rgb, (GlowFx)g.fx, g.pct, g.ms, g.period, g.lo);
  moodUntil = now + (holdMs ? holdMs : random(5000, 10001));   // Eillik: a mood outlasts the touch
  Serial.printf("[react] in=%s  face=%d %s  motion=%s(%ukf)  snd=%s  glow=%s\n", in,
                face < FACE_COUNT ? face + 1 : 0, face < FACE_COUNT ? FACE_NAMES[face] : "-", mname, nkf, sbuf,
                g.name ? g.name : "none");
}

void moodEnd() {
  moodUntil = 0;
  pendingFace = FACE_COUNT;
  if (faceCurrent() != baseFace) faceSet(baseFace);
}

bool motionAllowed() { return !S.quiet && S.batteryPct > 10 && !S.asleep && !otaRunning(); }
bool isOnline() { return S.mqttUp; }

// ===========================================================================
// sleep / wake
// ===========================================================================
static void goToSleep(uint32_t now, const char* why) {
  if (S.asleep) return;
  S.asleep = true;
  asleepAt = now;
  baseFace = FACE_SLEEPING;
  faceSet(FACE_SLEEPING);
  bodyMotion(MOT_FACE(FACE_SLEEPING), P_IDLE);          // settle to REST, then limp
  if (!S.quiet) sfxPlay(FACE_SLEEPING, 12, 2600);       // soft snore, -12 dB, never in quiet hours
  glowClearAll();
  glowSet(GR_SIDE, RGB_WARM, GFX_BREATHE, 5, 0, 5000, 1); // 5 % breathing at 0.2 Hz
  Serial.printf("[sleep] asleep (%s)\n", why);
}

static void wakeUp(uint32_t now) {
  if (!S.asleep) return;
  S.asleep = false;
  facePanel(true);
  audioSpeaker(true);
  bodyWake();
  sfxStop();
  glowClearAll();
  baseFace = FACE_SMILE;
  faceSet(FACE_SMILE);                                   // wake to SMILE, never a yawn (Eillik)
  if (explicitMode == MODE_SLEEP) explicitMode = MODE_IDLE;
  Serial.println("[sleep] awake");
}

void markChild(uint32_t now) {
  lastInteraction = now;
  S.lastChildMs = now;
  nextFidget = now + random(cal.fidgetMinS, cal.fidgetMaxS + 1) * 1000UL;
  if (S.asleep) wakeUp(now);
}

// ===========================================================================
// modes (§6.3) and the levels they set
// ===========================================================================
static bool inQuietHours() {
  if (!settings.quietStart[0] || !settings.quietEnd[0]) return false;
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  int m = lt.tm_hour * 60 + lt.tm_min;
  int s = atoi(settings.quietStart) * 60 + atoi(settings.quietStart + 3);
  int e = atoi(settings.quietEnd) * 60 + atoi(settings.quietEnd + 3);
  return s <= e ? (m >= s && m < e) : (m >= s || m < e);   // 20:30-07:00 wraps midnight
}

static void setExplicit(uint8_t m, uint32_t now) {
  if (m == MODE_SLEEP) { explicitMode = MODE_SLEEP; goToSleep(now, "set_mode sleep"); return; }
  if (S.asleep) wakeUp(now);
  uint8_t old = explicitMode;
  explicitMode = m;
  if (old == MODE_STORY && m != MODE_STORY) {
    glowClear(GR_BELLY);
    baseFace = FACE_SMILE;
    bodyStop(P_IDLE);
    moodEnd();
  }
  if (m == MODE_STORY) {                                 // STORYTELLER base face, amber belly glow 40 %
    baseFace = FACE_STORYTELLER;
    glowSet(GR_BELLY, RGB_AMBER, GFX_SOLID, 40);
    if (motionAllowed()) bodyMotion(MOT_FACE(FACE_STORYTELLER), P_IDLE);
    moodEnd();
  }
  if (m == MODE_PLAY) glowSet(GR_SIDE, RGB_WARM, GFX_SOLID, 20, 4000);
  if (m != MODE_QUIET && m != MODE_SLEEP) { settings.mode = m; settingsSave(); }
}

static void applyLevels(uint32_t now) {
  static uint32_t qAt = 0;
  if (now - qAt >= 1000) { qAt = now; quietHours = inQuietHours(); }
  S.quiet = quietHours || explicitMode == MODE_QUIET;
  uint8_t mode = S.asleep ? MODE_SLEEP : S.quiet ? MODE_QUIET : explicitMode == MODE_SLEEP ? MODE_IDLE : explicitMode;
  if (mode != S.mode) {
    Serial.printf("[mode] %s -> %s\n", MODES[S.mode], MODES[mode]);
    S.mode = mode;
  }
  // pixel brightness: the face's own level (§7.4) under the mode's cap (§9.1)
  uint8_t faceB = 100;
  switch (faceCurrent()) {
    case FACE_CRYING: case FACE_BORED: faceB = 70; break;
    case FACE_HEARTBROKEN: case FACE_TIRED: faceB = 60; break;
    case FACE_SLEEPY: faceB = 40; break;
    case FACE_SLEEPING: faceB = cal.brightSleep; break;
    default: break;
  }
  uint8_t cap = S.asleep ? cal.brightSleep : S.quiet ? cal.brightQuiet : cal.brightNormal;
  faceBrightness(min(faceB, cap));
  glowCap(S.asleep ? cal.glowCapSleep : S.quiet ? cal.glowCapQuiet : cal.glowCapNormal);

  static int lastLevel = -1;
  int level = settings.volume;
  if (S.quiet) level = min(level, 30);                   // quiet hours cap the level at 30 (§10.4)
  if (S.lowPower) level = max(0, level - 15);            // -6 dB in low-power mode (0.4 dB per step)
  if (level != lastLevel) { lastLevel = level; audioVolume(level); }

  if (S.asleep && now - asleepAt > 30000 && asleepAt) {  // 30 s into sleep: panel off, amp quiet
    asleepAt = 0;
    facePanel(false);
    sfxStop();
    audioSpeaker(false);
  }
}

// ===========================================================================
// idle life (§6.4) and the wind-down
// ===========================================================================
static const FaceId FIDGETS[] = {   // Eillik's 18, all positive
  FACE_GIDDY, FACE_PLAYFUL, FACE_MISCHIEVOUS, FACE_DERP, FACE_CHEEKY, FACE_WINK,
  FACE_STARRY, FACE_SPARKLE, FACE_LAUGH, FACE_WONDER, FACE_QUESTIONING,
  FACE_INVESTIGATING, FACE_HUNGRY, FACE_ANTICIPATION, FACE_EAGER, FACE_THOUGHTFUL,
  FACE_ECSTATIC, FACE_FLUSHED,
};
struct DecayStep { FaceId face; uint16_t ms; };
static const DecayStep DECAY_A[] = {{FACE_BORED, 6000}, {FACE_TIRED, 7000}, {FACE_YAWNING, 5000}};
static const DecayStep DECAY_B[] = {{FACE_THOUGHTFUL, 5000}, {FACE_BORED, 5000}, {FACE_SLEEPY, 5000}, {FACE_YAWNING, 4500}};
static const DecayStep DECAY_C[] = {{FACE_POUT, 4500}, {FACE_BORED, 5000}, {FACE_TIRED, 6000}, {FACE_YAWNING, 4500}};
static const struct { const DecayStep* s; uint8_t n; } DECAYS[] = {{DECAY_A, 3}, {DECAY_B, 4}, {DECAY_C, 4}};
static bool decayActive = false;
static uint8_t decayWhich = 0, decayIdx = 0;
static uint32_t decayStepUntil = 0;

static void idleLife(uint32_t now) {
  if (S.asleep || S.lifted || S.uploading || shuttingDown || otaRunning()) return;
  if (now - lastInteraction < 1000) decayActive = false;       // markChild() since the last step
  if (moodUntil && now > moodUntil && !decayActive) moodEnd();
  if (baseUntil && now > baseUntil) { baseUntil = 0; if (baseFace != FACE_STORYTELLER) baseFace = FACE_SMILE; moodEnd(); }

  bool idleMode = S.mode == MODE_IDLE || S.mode == MODE_QUIET;
  uint32_t windDown = (S.quiet ? cal.windDownQuietS : cal.windDownS) * 1000UL;
  if (decayActive) {
    if (now >= decayStepUntil) {
      if (decayIdx >= DECAYS[decayWhich].n) { decayActive = false; goToSleep(now, "wind-down"); return; }
      const DecayStep& st = DECAYS[decayWhich].s[decayIdx++];
      FaceId f = st.face;
      pendingFace = f; pendingAt = now;
      if (motionAllowed()) bodyMotion(MOT_FACE(f), P_IDLE);
      if (!S.turnOpen) sfxPlayFace(f);
      moodUntil = now + st.ms;
      decayStepUntil = now + st.ms;
      Serial.printf("[react] in=idle:wind-down  face=%d %s\n", f + 1, FACE_NAMES[f]);
    }
  } else if (idleMode && !moodUntil && baseFace == FACE_SMILE && now - lastInteraction > windDown) {
    decayActive = true;
    decayWhich = random(3);
    decayIdx = 0;
    decayStepUntil = now;
    Serial.printf("[decay] wind-down cycle %c\n", 'A' + decayWhich);
  } else if (!moodUntil && now > nextFidget && !bodyBusy(P_LOCAL)) {
    bool play = S.mode == MODE_PLAY;
    nextFidget = now + random(play ? 15 : cal.fidgetMinS, (play ? 25 : cal.fidgetMaxS) + 1) * 1000UL;
    FaceId f = FIDGETS[random(sizeof FIDGETS / sizeof FIDGETS[0])];
    react("idle:fidget", f, MOT_FACE(f), random(100) < 30 ? SND_FACE : S_NONE);   // sound 30 % of the time
  } else if (!moodUntil && now > nextGlance && !bodyBusy(P_IDLE) && motionAllowed()) {
    nextGlance = now + random(cal.glanceMinS, cal.glanceMaxS + 1) * 1000UL;
    bodyMotion(MOT_LOCAL(L_GLANCE), P_IDLE, random(2) ? BF_MIRROR : 0);
  }
}

// ===========================================================================
// battery (§7.5.5)
// ===========================================================================
static void powerReactions(uint32_t now) {
  static uint8_t last = 80;
  static uint32_t nextNag = 0;
  uint8_t lv = S.batteryPct;
  if (lv != last) { last = lv; nextNag = now; }
  if (lv <= 5 && !shuttingDown) {                        // rail low: say good night, then deep sleep
    shuttingDown = true;
    shutdownAt = now + 5000;
    react("power:battery_5", FACE_SLEEPING, NO_MOTION, S_NONE, "good_night");
    bodyLimp();
  }
  if (shuttingDown && now > shutdownAt) {
    Serial.println("[power] battery empty: deep sleep (cry alerts are already in NVS)");
    facePanel(false);
    audioSpeaker(false);
    delay(200);
    esp_deep_sleep_start();
  }
  if ((lv == 20 || lv == 10) && now >= nextNag && !moodUntil && !S.asleep && !S.turnOpen) {
    if (lv == 20) {
      react("power:battery_20", FACE_LOW_BATTERY, MOT_FACE(FACE_LOW_BATTERY), S_NONE, "im_hungry",
            {GR_BELLY, RGB_LOWBAT1, GFX_SOLID, 35, 6000, 0, 0, "belly:#FFB36A 35%"}, 0, 6000);
      nextNag = now + 600000;
    } else {
      react("power:battery_10", (now / 300000) & 1 ? FACE_LOW_BATTERY : FACE_SLEEPY, NO_MOTION, S_NONE, "plug_me_in",
            {GR_BELLY, RGB_LOWBAT2, GFX_SOLID, 25, 6000, 0, 0, "belly:#FF8A6A 25%"}, 0, 6000);
      nextNag = now + 300000;
    }
  }
}

// ===========================================================================
// voice turn (§5.2-5.6): the turn task reports, the face follows
// ===========================================================================
static int sayFace = -1;               // this turn's say expression
static uint32_t sayHoldUntil = 0;      // say landed before the first frame: held until it, or 3 s

static void sayResolve(bool ok) {      // the say paired with the turn (doneAt 0); G1 fallbacks have a time
  for (Pending& p : pend)
    if (p.used && p.action == A_SAY && !p.doneAt) { netAck(p.cmdId, ok ? ACK_DONE : ACK_FAILED); p.used = false; }
}

static void speakFace(FaceId f) { react("turn:speak", f, NO_MOTION, S_NONE, nullptr, NOGLOW, 0, 600000); }

static void turnEvents(uint32_t now) {
  TurnEvent e;
  while (turnNext(e)) switch (e.kind) {
    case TE_LISTEN:                    // audio is leaving: LISTENING + blue, nothing overrides it (§5.9)
      markChild(now);
      glowListening(60);
      faceSpeaking(false);
      sayFace = -1; sayHoldUntil = 0;
      if (faceCurrent() != FACE_LISTENING && pendingFace != FACE_LISTENING) react("turn:listen", FACE_LISTENING, NO_MOTION, S_NONE, nullptr, NOGLOW, 0, 600000);
      break;
    case TE_THINK:
      glowListening(0);
      react("turn:think", FACE_THINKING, NO_MOTION, S_NONE, nullptr,
            {GR_SIDE, RGB_THINK, GFX_BREATHE, 30, 0, 2000, 10, "side:think"}, 0, 600000);
      break;
    case TE_SPEAK:                     // frames before the say: speak on the current face, the say lands later (§5.2)
      glowClear(GR_SIDE);
      faceSpeaking(true);
      speakFace(sayFace >= 0 ? (FaceId)sayFace : faceCurrent());
      sayHoldUntil = 0;
      break;
    case TE_REPLY_END:
      faceSpeaking(false);
      sayResolve(e.a);
      sayFace = -1; sayHoldUntil = 0;
      break;
    case TE_FOLLOWUP:                  // LISTENING-lite: the mic is armed, 30 % blue (§5.6)
      glowListening(30);
      react("turn:followup", FACE_LISTENING, NO_MOTION, S_NONE, nullptr, NOGLOW, 0, 600000);
      break;
    case TE_IDLE:
      glowListening(0);
      glowClear(GR_SIDE);
      faceSpeaking(false);
      sayResolve(false);               // a say whose reply never came
      sayFace = -1; sayHoldUntil = 0;
      if (e.a == TI_SLOW) {            // THOUGHTFUL -> POUT-lite + "star_library_slow" (§5.3)
        react("turn:reply_slow", FACE_THOUGHTFUL, NO_MOTION, S_NONE, nullptr, NOGLOW, 0, 3000);
        later(1500, FACE_POUT, "star_library_slow");
      } else if (e.a == TI_ERROR) {
        react("turn:error", FACE_OFFLINE_SORRY, MOT_FACE(FACE_OFFLINE_SORRY), S_NONE, "star_library_sleeping", NOGLOW, 0, 4000);
      } else moodEnd();
      break;
  }
  // the held say shows after 3 s even without audio, but never over LISTENING (§5.9)
  if (sayHoldUntil && (int32_t)(now - sayHoldUntil) >= 0 && !S.uploading) { speakFace((FaceId)sayFace); sayHoldUntil = 0; }
}

// ===========================================================================
// cloud commands (§4.7). accepted / rejected already went out from the intake.
// ===========================================================================
static int exprFace(const char* e) {   // §9.3: Document B's known values first
  static const struct { const char* n; uint8_t f; } MAP[] = {
    {"happy", FACE_SMILE}, {"love", FACE_LOVING}, {"sleepy", FACE_SLEEPY}, {"curious", FACE_QUESTIONING},
    {"sad", FACE_TEARY}, {"listening", FACE_LISTENING}, {"excited", FACE_ECSTATIC}, {"giggle", FACE_GIDDY},
    {"laugh", FACE_LAUGH}, {"wonder", FACE_WONDER}, {"wow", FACE_WONDER}, {"scared", FACE_SCARED},
    {"thinking", FACE_THINKING}, {"shy", FACE_EMBARRASSED}, {"proud", FACE_SPARKLE}, {"playful", FACE_PLAYFUL},
    {"wink", FACE_WINK}, {"dizzy", FACE_CONFUSED}, {"angry", FACE_GRUMPY}, {"very_angry", FACE_FURIOUS},
    {"tired", FACE_TIRED}, {"bored", FACE_BORED}, {"hungry", FACE_HUNGRY}, {"comfort", FACE_COMFORT},
    {"calm", FACE_BREATHE}, {"storyteller", FACE_STORYTELLER},
  };
  for (auto& m : MAP) if (!strcmp(m.n, e)) return m.f;
  return faceFromName(e);              // and every face name in snake_case (G14)
}

static void execute(Cmd& c, uint32_t now) {
  switch (c.action) {
    case A_SAY: {   // paired with the open turn, else G1: expression + SFX for min(hint, 5 s)
      int f = exprFace(c.expression);
      if (f < 0) { Serial.printf("[cmd] say: unknown expression, keeping the face\n"); f = faceCurrent(); }
      if (c.voice == VOICE_GRANDMA) setExplicit(MODE_STORY, now);
      TurnState ts = turnState();
      if (ts != T_IDLE && ts != T_FOLLOWUP) {            // pair it with the open turn (§5.2); done after the drain
        if (c.voice == VOICE_GRANDMA) turnGrandma();
        addPending(c, 0);
        if (ts == T_PLAYING || ts == T_DRAINING) speakFace((FaceId)f);
        else { sayFace = f; sayHoldUntil = now + 3000; }
        break;
      }
      // no turn open: the G1 fallback
      react("cmd:say(G1)", (FaceId)f, NO_MOTION, SND_FACE, nullptr, NOGLOW, 0, max<uint32_t>(c.durationHintMs, 1500));
      addPending(c, now + max<uint32_t>(1, min<uint32_t>(c.durationHintMs, 5000)));
      break;
    }
    case A_ANIMATE: {
      int f = exprFace(c.expression);
      const char* e = c.eyes;
      if (!strcmp(e, "hearts")) f = !strcmp(c.expression, "love") ? FACE_LOVING : FACE_CHARMED;
      else if (!strcmp(e, "stars")) f = FACE_STARRY;
      else if (!strcmp(e, "closed")) f = FACE_SPARKLE;
      else if (!strcmp(e, "wink")) f = FACE_WINK;
      else if (!strcmp(e, "spiral")) f = FACE_CONFUSED;
      else if (!strcmp(e, "wide")) f = FACE_WONDER;
      if (f < 0) {
        Serial.println("[cmd] animate: unknown expression, keeping the face");
      } else {
        if (c.loop) { baseFace = (FaceId)f; baseUntil = now + 600000; }   // hold until changed (cap 10 min)
        bool idle = !bodyBusy(P_LOCAL);                  // its motion only if the body is idle (P4)
        react("cmd:animate", (FaceId)f, idle ? MOT_FACE(f) : NO_MOTION, S_NONE, nullptr, NOGLOW, 0, c.loop ? 600000 : 0);
      }
      if (!strcmp(e, "blink")) faceBlink();
      else if (!strcmp(e, "up")) faceGaze(0, -8);
      else if (!strcmp(e, "down")) faceGaze(0, 8);
      else if (!strcmp(e, "left")) faceGaze(16, 0);
      else if (!strcmp(e, "right")) faceGaze(-16, 0);
      netAck(c.cmdId, ACK_DONE);
      break;
    }
    case A_GESTURE: {
      int g = gestureFromName(c.name);
      if (g < 0) { Serial.printf("[cmd] gesture: unknown name\n"); netAck(c.cmdId, ACK_FAILED); break; }
      if (faceCurrent() == FACE_SMILE) { pendingFace = FACE_GIDDY; pendingAt = now; moodUntil = now + 4000; }
      bodyMotion(g, P_CLOUD, 0, addPending(c));          // done only when feedback confirms the pose
      break;
    }
    case A_MOVE: {
      uint8_t t = addPending(c);
      if (c.moveCmd == MV_STOP) { bodyDrive(DM_STOP, 0, 0, 0, true, t); break; }
      pendingFace = FACE_EAGER; pendingAt = now; moodUntil = now + 600000;
      if (c.moveCmd == MV_COME)
        bodyDrive(DM_COME, (int16_t)min(c.linearMms, 300.0f), 0, (uint16_t)min(c.maxDistMm, 2000.0f), true, t);
      else
        bodyDrive(DM_TURN, 0, (int16_t)min(c.angularMradS, cal.wTurn * 1000), (uint16_t)min(c.maxDistMm, 60000.0f), true, t);
      break;
    }
    case A_PLAY: {
      if (c.source == SRC_STREAM) {                      // G2: no transport for a streamed play
        Serial.println("[cmd] play source=stream: no transport in proto_ver 2 (G2)");
        netAck(c.cmdId, ACK_FAILED);
        break;
      }
      if (!strcmp(c.contentId, "bringup_probe")) {       // the matrix probe: Eillik's boot jingle
        sfxPlay(S_BOOT);
        netAck(c.cmdId, ACK_DONE);                       // done = playback started (G3)
        break;
      }
      int n = !strncmp(c.contentId, "dance_", 6) ? atoi(c.contentId + 6) : 0;
      if (n >= 1 && n <= 10) {                           // reserved local game id (§7.8, G23)
        setExplicit(MODE_PLAY, now);
        react("cmd:play dance", FACE_STARRY, NO_MOTION, S_NONE, nullptr, {GR_SIDE, 0, GFX_RAINBOW, 40, 20000, 0, 0, "side:rainbow"});
        bodyDance(n, 100, 8, P_CLOUD);
        netAck(c.cmdId, ACK_DONE);
        break;
      }
      // ponytail: SD songs/stories (Ogg-Opus + cues) and the copycat/freeze/says games come
      // with the Opus decoder; until then the content is "missing" on this build
      Serial.printf("[cmd] play %s: not on this build yet\n", c.contentId);
      netAck(c.cmdId, ACK_FAILED);
      break;
    }
    case A_SET_MODE:
      setExplicit(c.mode, now);
      netAck(c.cmdId, ACK_DONE);
      break;
    case A_LED:   // drawn on screen (§8); ears wait while the privacy indicator is on
      glowCloud(c.ledTarget, c.rgb, c.ledEffect, c.brightness);
      Serial.printf("[cmd] led %s #%06lX %s %u%%\n", LED_TARGETS[c.ledTarget], (unsigned long)c.rgb, LED_EFFECTS[c.ledEffect], c.brightness);
      netAck(c.cmdId, ACK_DONE);
      break;
    case A_VOLUME:
      settings.volume = c.level;
      settingsSave();
      if (!S.quiet) sfxPlay(S_VOL);                      // chirp at the new level
      netAck(c.cmdId, ACK_DONE);
      break;
    case A_CONFIG:
      if (c.cfg & CFG_VOLUME) settings.volume = c.level;
      if (c.cfg & CFG_QUIET) { strcpy(settings.quietStart, c.quietStart); strcpy(settings.quietEnd, c.quietEnd); }
      if (c.cfg & CFG_MOVEMENT) settings.movementOn = c.movementOn;
      if (c.cfg & CFG_LANGUAGE) strcpy(settings.language, c.language);
      if (c.cfg & CFG_CHILD) { settings.childSet = !c.childNull; strcpy(settings.childId, c.childNull ? "" : c.childId); }
      settingsSave();
      netAck(c.cmdId, ACK_DONE);
      break;
    case A_STOP_AUDIO:   // 8 ms fade, cancel the pending say
      sfxStop();
      turnStopAudio();
      for (Pending& p : pend) if (p.used && p.action == A_SAY) { netAck(p.cmdId, ACK_DONE); p.used = false; }
      netAck(c.cmdId, ACK_DONE);
      break;
    case A_OTA:
      if (!otaStart(c.cmdId, c.manifestUrl)) { netAck(c.cmdId, ACK_FAILED); break; }
      react("cmd:ota", FACE_UPDATING, NO_MOTION, S_NONE, nullptr,
            {GR_BELLY, RGB_GREEN, GFX_SOLID, 20, 0, 0, 0, "belly:green 20%"}, 0, 600000);
      bodyMotion(MOT_FACE(FACE_UPDATING), P_SAFETY);     // servos hold, then limp
      break;
  }
}

// ===========================================================================
void behaviorBegin() {
  uint32_t now = millis();
  explicitMode = settings.mode <= MODE_STORY ? settings.mode : MODE_IDLE;
  lastInteraction = now;
  nextGlance = now + 8000;
  nextFidget = now + random(cal.fidgetMinS, cal.fidgetMaxS + 1) * 1000UL;
  faceSet(FACE_SMILE);
  sfxPlayClip(CLIP_HELLO);                              // hello clip (prototype voice, §10.3)
  glowSet(GR_SIDE, RGB_WARM, GFX_SWELL, 40, 2000);       // warm white side glow fades in
  if (!S.lifted && motionAllowed()) { bodyMotion(G_STRETCH, P_IDLE); bootStep = 1; }   // stretch, then wave
  Serial.println("[boot] SMILE, hello, stretch -> wave");
}

void behaviorFace(FaceId f) {
  markChild(millis());
  react("console", f, MOT_FACE(f), SND_FACE);
}

void behaviorGesture(uint8_t g) { bodyMotion(g, P_LOCAL); }

void behaviorDance(uint8_t pattern, uint16_t bpm) {
  markChild(millis());
  react("console:dance", FACE_STARRY, NO_MOTION, S_NONE);
  bodyDance(pattern, bpm, 8, P_LOCAL);
}

void behaviorSetMode(uint8_t mode) { setExplicit(mode, millis()); }

void behaviorPrintState(Print& out) {
  out.printf("mode %s%s%s | face %d %s | base %s | mood %lds | battery %u%% rail %u mV | lifted %d quiet %d asleep %d | fps %u\n",
             MODES[S.mode], quietHours ? " (quiet hours)" : "", S.lowPower ? " LOW-POWER" : "",
             faceCurrent() + 1, FACE_NAMES[faceCurrent()], FACE_NAMES[baseFace],
             moodUntil ? (long)(moodUntil - millis()) / 1000 : 0L, S.batteryPct, S.railMv, S.lifted, S.quiet,
             S.asleep, faceFps());
}

void behaviorLoop(uint32_t now) {
  SenseEvent se;
  while (senseNext(se)) reactSense(se, now);
  BodyEvent be;
  while (bodyNext(be)) {
    if ((be.kind == BE_MOTION_DONE || be.kind == BE_DRIVE_DONE) && be.ticket) {
      resolve(be.ticket, be.ok);
      if (be.kind == BE_DRIVE_DONE) { pendingFace = FACE_SMILE; pendingAt = now; moodUntil = now + 3000; }
    }
    if (be.kind == BE_MOTION_DONE && bootStep == 1 && be.motion == G_STRETCH) {
      bootStep = 0;
      if (motionAllowed()) bodyMotion(G_WAVE, P_IDLE);
    }
    if (be.kind == BE_MOTION_DONE && be.motion == 0xFFFE && explicitMode == MODE_PLAY) setExplicit(MODE_IDLE, now);
    reactBody(be, now);
  }
  static Cmd cmd;
  while (netNextCmd(cmd)) execute(cmd, now);
  turnEvents(now);
  for (Pending& p : pend)                                // say fallback: done after its duration
    if (p.used && p.doneAt && (int32_t)(now - p.doneAt) >= 0) { netAck(p.cmdId, ACK_DONE); p.used = false; }
  if (!otaRunning() && faceCurrent() == FACE_UPDATING) { glowClear(GR_BELLY); bodyWake(); moodEnd(); }

  if (pendingFace < FACE_COUNT && (int32_t)(now - pendingAt) >= 0) { faceSet(pendingFace); pendingFace = FACE_COUNT; }
  applyLevels(now);
  powerReactions(now);
  reactionsTick(now);
  idleLife(now);
  faceEnvelope(audioEnvelope());                       // the mouth follows the speaker's own samples (§5.11)
  faceUpdate(now);
}
