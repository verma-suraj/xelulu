// Every movement as data, transcribed from FW-SOT §7.3 (gestures), §7.4
// (each face's default motion) and §7.5 (local reactions). Angles: HP 0 =
// forward, + = robot's left; arms 0 = hanging, 10 = rest, 90 = forward,
// 150 = up. Times in ms. Per joint, segments never overlap in time.
#include "Motions.h"
#include "../audio/Sfx.h"

#define M_MV(t0, t1, m, e, h, l, r)        {t0, t1, KF_MV, m, e, h, l, r}
#define M_OSC(t0, t1, m, e, lo, hi, half)  {t0, t1, KF_OSC, m, (uint8_t)(e), lo, hi, half}
#define M_TRM(t0, t1, m, amp, hz10, decay) {t0, t1, KF_TRM, m, 0, amp, hz10, decay}
#define M_EYE(t, x, y, tilt)               {t, t, KF_EYE, 0, 0, x, y, tilt}
#define M_SND(t, id)                       {t, t, KF_SND, 0, 0, id, 0, 0}
#define M_REST(t0, t1)                     M_MV(t0, t1, J_ALL, E_EIO, 0, 10, 10)
#define M_ARMS_REST(t0, t1)                M_MV(t0, t1, J_ARMS, E_EIO, 0, 10, 10)
#define M_LOOP(t, back)                    {t, t, KF_LOOP, 0, 0, back, 0, 0}
#define M_WAIT5(t0, t1, m)                 {t0, t1, KF_WAIT5, m, 0, 0, 0, 0}
#define M_TORQ(t, m, p)                    {t, t, KF_TORQ, m, 0, p, 0, 0}
#define M_AIM(t0, t1, e, off, lim)         {t0, t1, KF_MV, J_HP, (uint8_t)((e) | E_AIM), off, lim, 0}
#define LOOPS 0xFFFF

// ============================== gestures (§7.3) ==============================
static const Kf K_WAVE[] = {   // right arm; wave_left mirrors it
  M_MV(0, 200, J_HP, E_EO, -10, 0, 0), M_EYE(0, 0, -4, 0),
  M_MV(120, 380, J_AR, E_EO, 0, 0, 135),
  M_OSC(380, 1460, J_AR, E_EIO, 100, 135, 180),        // 135 <-> 100 x3, 180 ms halves
  M_REST(1460, 1860), M_EYE(1460, 0, 0, 0),
};
static const Kf K_HIGH_FIVE[] = {   // interactive: a right_hand tap or an AR load spike is the five
  M_MV(100, 320, J_AR, E_EO, 0, 0, 125), M_EYE(100, 0, -4, 0),
  M_WAIT5(320, 3320, J_AR),
  M_REST(3320, 3720), M_EYE(3320, 0, 0, 0),
};
static const Kf K_HUG[] = {
  M_MV(100, 600, J_ALL, E_EIO, 0, 60, 60), M_EYE(100, 0, -2, 0),           // OPEN_ARMS
  M_TORQ(600, J_ARMS, TP_GENTLE),
  M_MV(600, 1200, J_ALL, E_EIO, 0, 85, 85), M_EYE(600, 0, 4, 0),           // HUG
  M_TORQ(2700, J_ARMS, TP_NORMAL), M_REST(2700, 3300), M_EYE(2700, 0, 0, 0),
};
static const Kf K_CLAP[] = {   // lift-only arms cannot meet: the sound sells the clap
  M_MV(0, 120, J_ARMS, E_EO, 0, 60, 60),
  M_OSC(120, 1400, J_ARMS, E_EIO, 95, 60, 160),        // closes at 280, 600, 920, 1240
  M_SND(280, S_CLAP), M_SND(600, S_CLAP), M_SND(920, S_CLAP), M_SND(1240, S_CLAP),
  M_EYE(280, 0, 2, 0), M_EYE(360, 0, 0, 0), M_EYE(600, 0, 2, 0), M_EYE(680, 0, 0, 0),
  M_EYE(920, 0, 2, 0), M_EYE(1000, 0, 0, 0), M_EYE(1240, 0, 2, 0), M_EYE(1320, 0, 0, 0),
  M_ARMS_REST(1400, 1650),
};
static const Kf K_POINT_LEFT[] = {
  M_MV(0, 250, J_HP, E_EO, 30, 0, 0), M_EYE(0, 12, 0, 0),
  M_MV(125, 450, J_AL, E_EO, 0, 90, 0),
  M_REST(1650, 2050), M_EYE(1650, 0, 0, 0),
};
static const Kf K_NOD[] = {    // no tilt joint: the eyes dip, the arms bob (§7.1)
  M_EYE(0, 0, 8, 0), M_EYE(212, 0, 0, 0), M_EYE(425, 0, 8, 0), M_EYE(637, 0, 0, 0),
  M_OSC(0, 850, J_ARMS, E_EIO, 18, 10, 212),
};
static const Kf K_SHAKE[] = {
  M_MV(0, 180, J_HP, E_EIO, 20, 0, 0), M_MV(180, 480, J_HP, E_EIO, -20, 0, 0),
  M_MV(480, 760, J_HP, E_EIO, 16, 0, 0), M_MV(760, 1000, J_HP, E_EIO, 0, 0, 0),
};
static const Kf K_CELEBRATE[] = {
  M_MV(100, 350, J_ARMS, E_EO, 0, 145, 145), M_EYE(100, 0, -4, 0),
  M_OSC(350, 1350, J_ARMS, E_LIN | E_ANTI, 135, 150, 62),   // alternating +-10 at 4 Hz
  M_ARMS_REST(1350, 1750), M_EYE(1350, 0, 0, 0),
};
static const Kf K_SHRUG[] = {
  M_MV(0, 150, J_ARMS, E_EO, 0, 40, 40), M_OSC(0, 600, J_HP, E_EIO, 8, -8, 150), M_EYE(0, 0, -2, 0),
  M_REST(650, 900), M_EYE(650, 0, 0, 0),
};
static const Kf K_THINK[] = {   // HAND_TO_FACE_R
  M_MV(0, 600, J_ALL, E_EIO, -10, 10, 115), M_EYE(0, 10, -6, 0),
  M_REST(2600, 3100), M_EYE(2600, 0, 0, 0),
};
static const Kf K_SCARED_G[] = {   // hands over eyes, tremble, peek
  M_MV(0, 180, J_ALL, E_EO, 0, 130, 130), M_EYE(0, 0, 6, 0),
  M_TRM(180, 1500, J_ARMS, 4, 80, 0),
  M_MV(1500, 1700, J_AL, E_EIO, 0, 100, 0),
  M_REST(1900, 2300), M_EYE(1900, 0, 0, 0),
};
static const Kf K_STRETCH[] = {
  M_MV(0, 900, J_ALL, E_EIO, 0, 150, 150), M_EYE(0, 0, -8, 0),
  M_REST(1900, 3100), M_EYE(1900, 0, 0, 0),
};
static const Kf K_TILT_CURIOUS[] = {   // HP +15 and one eye higher: the "face tilts" look
  M_MV(0, 400, J_HP, E_EIO, 15, 0, 0), M_EYE(0, 0, 0, 6),
  M_REST(1600, 2000), M_EYE(1600, 0, 0, 0),
};
static const Kf K_LOOK_UP[] = {M_EYE(0, 0, -8, 0), M_EYE(1300, 0, 0, 0)};
static const Kf K_LOOK_DOWN[] = {M_EYE(0, 0, 8, 0), M_EYE(1300, 0, 0, 0)};
static const Kf K_LOOK_LEFT[] = {
  M_MV(0, 300, J_HP, E_EIO, 35, 0, 0), M_EYE(0, 15, 0, 0),
  M_REST(1300, 1700), M_EYE(1300, 0, 0, 0),
};
static const Kf K_BOW[] = {
  M_MV(0, 500, J_ARMS, E_EIO, 0, 40, 40), M_EYE(0, 0, 6, 0),
  M_REST(1300, 1800), M_EYE(1300, 0, 0, 0),
};

#define MO(k, dur, name) {k, sizeof(k) / sizeof(Kf), dur, name}
static const Motion GESTURES[G_COUNT] = {
  MO(K_WAVE, 1860, "wave"), MO(K_HIGH_FIVE, 3720, "high_five"), MO(K_HUG, 3300, "hug"),
  MO(K_CLAP, 1650, "clap"), MO(K_POINT_LEFT, 2050, "point_left"), MO(K_NOD, 850, "nod"),
  MO(K_SHAKE, 1000, "shake"),
  MO(K_WAVE, 1860, "wave_left"), MO(K_HIGH_FIVE, 3720, "high_five_left"), MO(K_POINT_LEFT, 2050, "point_right"),
  MO(K_CELEBRATE, 1750, "celebrate"), MO(K_SHRUG, 900, "shrug"), MO(K_THINK, 3100, "think"),
  MO(K_SCARED_G, 2300, "scared"), MO(K_STRETCH, 3100, "stretch"), MO(K_TILT_CURIOUS, 2000, "tilt_curious"),
  MO(K_LOOK_UP, 1700, "look_up"), MO(K_LOOK_DOWN, 1700, "look_down"), MO(K_LOOK_LEFT, 1700, "look_left"),
  MO(K_LOOK_LEFT, 1700, "look_right"), MO(K_BOW, 1800, "bow"),
};
const char* const GESTURE_NAMES[G_COUNT] = {
  "wave", "high_five", "hug", "clap", "point_left", "nod", "shake", "wave_left", "high_five_left",
  "point_right", "celebrate", "shrug", "think", "scared", "stretch", "tilt_curious", "look_up",
  "look_down", "look_left", "look_right", "bow",
};

// ===================== each face's default motion (§7.4) =====================
static const Kf F_GIDDY[] = {
  M_OSC(0, 280, J_HP, E_EIO, 10, -10, 70), M_MV(280, 400, J_HP, E_EIO, 0, 0, 0),
  M_MV(100, 240, J_ARMS, E_EO, 0, 45, 45), M_MV(240, 380, J_ARMS, E_EIO, 0, 10, 10),
};
static const Kf F_ECSTATIC[] = {
  M_OSC(0, 1400, J_HP, E_EIO, 25, -25, 350), M_MV(1400, 1700, J_HP, E_EIO, 0, 0, 0),
  M_MV(100, 330, J_ARMS, E_EO, 0, 145, 145), M_EYE(100, 0, -4, 0),
  M_OSC(330, 930, J_ARMS, E_EIO | E_ANTI, 137, 150, 100),   // shake +-8 x3
  M_ARMS_REST(930, 1800), M_EYE(930, 0, 0, 0),
};
static const Kf F_CHARMED[] = {   // HP toward the touch, slow (mirror for the other side)
  M_MV(0, 800, J_HP, E_EIO, 15, 0, 0), M_MV(0, 600, J_ARMS, E_EIO, 0, 70, 70),
  M_REST(2000, 2600),
};
static const Kf F_SPARKLE[] = {
  M_MV(0, 300, J_HP, E_EO, 30, 0, 0), M_MV(300, 1200, J_HP, E_EO, -30, 0, 0),
  M_MV(1200, 1600, J_HP, E_EIO, 0, 0, 0),
  M_MV(100, 380, J_ARMS, E_SPR, 0, 130, 130), M_OSC(380, 780, J_ARMS, E_EIO, 136, 124, 100),
  M_ARMS_REST(780, 1300),
};
static const Kf F_WINK[] = {
  M_MV(0, 300, J_HP, E_EIO, -12, 0, 0), M_MV(300, 600, J_HP, E_EIO, 0, 0, 0),
  M_MV(0, 150, J_AR, E_EO, 0, 0, 110), M_MV(150, 300, J_AR, E_EIO, 0, 0, 80),
  M_MV(300, 450, J_AR, E_EIO, 0, 0, 110), M_MV(450, 600, J_AR, E_EIO, 0, 0, 10),
};
static const Kf F_STARRY[] = {
  M_OSC(0, 2000, J_HP, E_EIO, 15, -15, 500),
  M_OSC(0, 2000, J_ARMS, E_EIO | E_ANTI, 30, 100, 250),   // alternating at 2 Hz
  M_REST(2000, 2400),
};
static const Kf F_LAUGH[] = {
  M_OSC(0, 660, J_HP, E_EIO, 8, -8, 110),
  M_OSC(0, 660, J_ARMS, E_EIO | E_ANTI, 25, 45, 110),
  M_EYE(0, 0, 2, 0), M_EYE(166, 0, 0, 0), M_EYE(333, 0, 2, 0), M_EYE(500, 0, 0, 0),
  M_REST(660, 900),
};
static const Kf F_EAGER[] = {   // HP toward the source
  M_MV(0, 300, J_HP, E_EO, 20, 0, 0), M_MV(0, 300, J_ARMS, E_EO, 0, 60, 60), M_EYE(0, 0, 4, 0),
  M_REST(1500, 1800), M_EYE(1500, 0, 0, 0),
};
static const Kf F_ANTICIP[] = {
  M_MV(0, 400, J_HP, E_EIO, 10, 0, 0), M_MV(0, 250, J_ARMS, E_EO, 0, 35, 35),
  M_REST(1750, 2000),
};
static const Kf F_POUT[] = {   // HP away
  M_MV(0, 300, J_HP, E_EIO, -20, 0, 0), M_MV(0, 300, J_ARMS, E_EIO, 0, 0, 0), M_EYE(0, 0, 4, 0),
  M_REST(1700, 2000), M_EYE(1700, 0, 0, 0),
};
static const Kf F_TEARY[] = {   // the right hand wipes the eye
  M_MV(0, 900, J_HP, E_EIO, -15, 0, 0), M_TRM(0, 1900, J_HP, 2, 30, 0),
  M_MV(0, 400, J_AL, E_EIO, 0, 0, 0), M_MV(0, 900, J_AR, E_EIO, 0, 0, 95),
  M_OSC(900, 1300, J_AR, E_EIO, 87, 95, 100), M_MV(1300, 1900, J_AR, E_EIO, 0, 0, 10),
  M_EYE(0, 0, 6, 0),
  M_MV(1900, 2200, J_HP | J_AL, E_EIO, 0, 10, 0), M_EYE(1900, 0, 0, 0),
};
static const Kf F_CRYING[] = {
  M_MV(0, 300, J_ARMS, E_EO, 0, 125, 125), M_TRM(300, 1900, J_ARMS, 3, 15, 0),
  M_OSC(0, 1900, J_HP, E_EIO, 5, -5, 333),
  M_REST(1900, 2200),
};
static const Kf F_DISAPP[] = {
  M_MV(0, 400, J_HP, E_EIO, -25, 0, 0), M_MV(0, 100, J_ARMS, E_EO, 0, 0, 0), M_EYE(0, 0, 8, 0),
  M_REST(2300, 3050), M_EYE(2300, 0, 0, 0),
};
static const Kf F_HEARTBRK[] = {
  M_MV(0, 1400, J_HP, E_EIO, -40, 0, 0), M_TORQ(0, J_ARMS, TP_GENTLE), M_MV(0, 700, J_ARMS, E_EIO, 0, 0, 0),
  M_TORQ(2500, J_ARMS, TP_NORMAL), M_REST(2500, 3900),
};
static const Kf F_GRUMPY[] = {   // acting only: HP stubborn at 0
  M_MV(0, 250, J_ARMS, E_EO, 0, 55, 55), M_ARMS_REST(2250, 2500),
};
static const Kf F_FURIOUS[] = {   // acting only
  M_TRM(0, 1000, J_HP, 4, 60, 0), M_MV(0, 220, J_ARMS, E_EO, 0, 110, 110),
  M_TRM(220, 1000, J_ARMS, 5, 60, 0), M_REST(1000, 1300),
};
static const Kf F_FUMING[] = {   // acting only
  M_TRM(0, 900, J_HP, 8, 70, 0), M_MV(0, 180, J_ARMS, E_EO, 0, 145, 145),
  M_TRM(180, 900, J_ARMS, 7, 70, 0), M_REST(900, 1200),
};
static const Kf F_ANNOYED[] = {
  M_MV(0, 160, J_HP, E_EO, -15, 0, 0), M_MV(0, 160, J_ARMS, E_EO, 0, 45, 45),
  M_MV(1160, 1320, J_HP, E_EO, 0, 0, 0), M_ARMS_REST(1400, 1800),
};
static const Kf F_FLUSHED[] = {   // bashful
  M_OSC(0, 600, J_HP, E_EIO, 6, -6, 150), M_MV(600, 750, J_HP, E_EIO, 0, 0, 0),
  M_MV(0, 200, J_ARMS, E_EO, 0, 120, 120), M_MV(200, 450, J_ARMS, E_EIO, 0, 70, 70),
  M_ARMS_REST(800, 1200),
};
static const Kf F_QUESTION[] = {
  M_MV(0, 300, J_HP, E_EIO, 15, 0, 0), M_MV(0, 250, J_AR, E_EO, 0, 0, 90), M_EYE(0, 0, 0, 6),
  M_REST(1750, 2000), M_EYE(1750, 0, 0, 0),
};
static const Kf F_INVESTIG[] = {   // pointing while it sweeps
  M_MV(0, 400, J_HP, E_EIO, 30, 0, 0), M_MV(400, 2000, J_HP, E_LIN, -30, 0, 0),
  M_MV(0, 300, J_AL, E_EO, 0, 90, 0), M_REST(2000, 2400),
};
static const Kf F_THOUGHT[] = {   // hand to chin
  M_MV(0, 500, J_HP, E_EIO, -8, 0, 0), M_MV(0, 500, J_AR, E_EIO, 0, 0, 115), M_EYE(0, 10, -6, 0),
  M_REST(2300, 2800), M_EYE(2300, 0, 0, 0),
};
static const Kf F_CONFUSED[] = {   // dizzy
  M_OSC(0, 1000, J_HP, E_EIO, 15, -15, 166),
  M_OSC(0, 1080, J_ARMS, E_EIO | E_ANTI, 20, 70, 180),
  M_REST(1080, 1400),
};
static const Kf F_WONDER[] = {
  M_MV(0, 450, J_HP, E_EIO, 10, 0, 0), M_MV(0, 450, J_ARMS, E_EIO, 0, 120, 120), M_EYE(0, 0, -8, 0),
  M_REST(2050, 2500), M_EYE(2050, 0, 0, 0),
};
static const Kf F_PLAYFUL[] = {
  M_OSC(0, 1000, J_HP, E_EIO, 12, -12, 166),
  M_MV(0, 150, J_AR, E_EO, 0, 0, 60), M_OSC(150, 1150, J_AR, E_EIO, 100, 60, 166),
  M_EYE(0, 0, -2, 0), M_EYE(250, 0, 0, 0), M_EYE(500, 0, -2, 0), M_EYE(750, 0, 0, 0),
  M_REST(1150, 1400),
};
static const Kf F_MISCHIEF[] = {   // slow side-eye, rubbing hands
  M_MV(0, 900, J_HP, E_EIO, -10, 0, 0), M_EYE(0, -10, 0, 0),
  M_MV(0, 300, J_ARMS, E_EO, 0, 50, 50), M_OSC(300, 900, J_ARMS, E_EIO | E_ANTI, 44, 56, 100),
  M_REST(900, 1200), M_EYE(900, 0, 0, 0),
};
static const Kf F_CHEEKY[] = {
  M_MV(0, 140, J_HP, E_EO, 15, 0, 0), M_MV(140, 280, J_HP, E_EIO, 0, 0, 0),
  M_MV(0, 140, J_AR, E_EO, 0, 0, 120), M_MV(440, 580, J_AR, E_EIO, 0, 0, 10),
};
static const Kf F_DERP[] = {   // loose, gentle torque
  M_TORQ(0, J_ALL, TP_GENTLE),
  M_MV(0, 220, J_HP, E_EIO, 15, 0, 0), M_MV(220, 440, J_HP, E_EIO, -10, 0, 0),
  M_MV(440, 660, J_HP, E_EIO, 12, 0, 0), M_MV(660, 880, J_HP, E_EIO, 0, 0, 0),
  M_MV(0, 220, J_AL, E_EIO, 0, 55, 0), M_MV(220, 440, J_AL, E_EIO, 0, 15, 0),
  M_MV(440, 660, J_AL, E_EIO, 0, 40, 0), M_MV(660, 880, J_AL, E_EIO, 0, 10, 0),
  M_MV(0, 220, J_AR, E_EIO, 0, 0, 50), M_MV(220, 440, J_AR, E_EIO, 0, 0, 10),
  M_MV(440, 660, J_AR, E_EIO, 0, 0, 45), M_MV(660, 880, J_AR, E_EIO, 0, 0, 10),
  M_TORQ(880, J_ALL, TP_NORMAL),
};
static const Kf F_LOVING[] = {   // HUG pose, HP toward the touch
  M_MV(0, 400, J_HP, E_EIO, 10, 0, 0), M_MV(0, 400, J_ARMS, E_EIO, 0, 85, 85), M_EYE(0, 0, 4, 0),
  M_REST(2400, 2800), M_EYE(2400, 0, 0, 0),
};
static const Kf F_TIRED[] = {
  M_MV(0, 1000, J_HP, E_EIO, -10, 0, 0), M_TORQ(0, J_ARMS, TP_GENTLE),
  M_MV(0, 600, J_ARMS, E_EIO, 0, 0, 0), M_EYE(0, 0, 6, 0),
  M_TORQ(2600, J_ARMS, TP_NORMAL), M_REST(2600, 3200), M_EYE(2600, 0, 0, 0),
};
static const Kf F_SLEEPING[] = {M_REST(0, 600), M_TORQ(700, J_ALL, TP_LIMP)};
static const Kf F_SLEEPY[] = {   // nodding off sideways, snap back
  M_MV(0, 1500, J_HP, E_LIN, 8, 0, 0), M_MV(1500, 1650, J_HP, E_EO, 0, 0, 0),
  M_MV(0, 500, J_ARMS, E_EIO, 0, 35, 35), M_MV(500, 1000, J_ARMS, E_EIO, 0, 5, 5),
  M_ARMS_REST(1650, 2000),
};
static const Kf F_SCARED[] = {   // snap away, guard, decaying tremor
  M_MV(0, 160, J_HP, E_EO, -25, 0, 0), M_TRM(160, 1060, J_HP, 8, 80, 1),
  M_MV(0, 160, J_ARMS, E_EO, 0, 125, 125),
  M_ARMS_REST(660, 980), M_MV(1060, 1400, J_HP, E_EIO, 0, 0, 0),
};
static const Kf F_BORED[] = {   // loops: a single-arm shrug now and then
  M_MV(0, 800, J_HP, E_EIO, -6, 0, 0), M_MV(0, 800, J_ARMS, E_EIO, 0, 5, 5),
  M_MV(5000, 5250, J_AL, E_EIO, 0, 30, 0), M_MV(5250, 5500, J_AL, E_EIO, 0, 5, 0),
  M_MV(11500, 11750, J_AR, E_EIO, 0, 0, 30), M_MV(11750, 12000, J_AR, E_EIO, 0, 0, 5),
  M_LOOP(12000, 800),
};
static const Kf F_EMBARR[] = {   // left arm covers the face
  M_MV(0, 300, J_HP, E_EO, -25, 0, 0), M_TRM(0, 500, J_HP, 4, 60, 1),
  M_MV(0, 300, J_AL, E_EO, 0, 125, 0), M_MV(0, 300, J_AR, E_EO, 0, 0, 0), M_EYE(0, 0, 6, 0),
  M_REST(1650, 1900), M_EYE(1650, 0, 0, 0),
};
static const Kf F_HUNGRY[] = {   // "gimme" twice, eyes on the belly
  M_MV(0, 300, J_ARMS, E_EIO, 0, 40, 40), M_EYE(0, 0, 6, 0),
  M_MV(300, 700, J_ARMS, E_EIO, 0, 80, 80), M_MV(700, 1100, J_ARMS, E_EIO, 0, 40, 40),
  M_MV(1800, 2200, J_ARMS, E_EIO, 0, 80, 80), M_MV(2200, 2600, J_ARMS, E_EIO, 0, 40, 40),
  M_ARMS_REST(2600, 3000), M_EYE(2600, 0, 0, 0),
};
static const Kf F_LISTENING[] = {   // ALERT, head toward the voice; held while listening
  M_AIM(0, 400, E_EIO, 0, 60), M_MV(0, 300, J_ARMS, E_EO, 0, 25, 25), M_EYE(0, 0, -6, 0),
  M_LOOP(500, 450),
};
static const Kf F_THINKING[] = {M_REST(0, 600), M_EYE(0, 10, -6, 0), M_LOOP(700, 650)};
static const Kf F_COMFORT[] = {   // toward the sound, OPEN_ARMS, slow sway
  M_AIM(0, 800, E_EIO, 0, 60), M_MV(0, 600, J_ARMS, E_EIO, 0, 60, 60), M_EYE(0, 0, -2, 0),
  M_OSC(800, 5800, J_HP, E_EIO | E_REL, 6, -6, 2500),
  M_LOOP(5800, 800),
};
static const Kf F_BREATHE[] = {   // 4 s inhale, 4 s exhale
  M_MV(0, 4000, J_ARMS, E_EIO, 0, 40, 40), M_MV(4000, 8000, J_ARMS, E_EIO, 0, 10, 10),
  M_LOOP(8000, 0),
};
static const Kf F_STORY[] = {M_OSC(0, 6666, J_HP, E_EIO, 8, -8, 3333), M_LOOP(6666, 0)};
static const Kf F_UPSIDE[] = {   // arms flail +-20 at 1.5 Hz while inverted
  M_MV(0, 200, J_ARMS, E_EO, 0, 40, 40), M_OSC(200, 1533, J_ARMS, E_EIO | E_ANTI, 20, 60, 333),
  M_LOOP(1533, 200),
};
static const Kf F_OFFLINE[] = {   // eyes down, then GIDDY's motion
  M_EYE(0, 0, 4, 0), M_EYE(1500, 0, 0, 0),
  M_OSC(1500, 1780, J_HP, E_EIO, 10, -10, 70), M_MV(1780, 1900, J_HP, E_EIO, 0, 0, 0),
  M_MV(1600, 1740, J_ARMS, E_EO, 0, 45, 45), M_ARMS_REST(1740, 1880),
};
static const Kf F_UPDATING[] = {M_TORQ(2000, J_ALL, TP_LIMP)};

static const Motion FACE_MOTIONS[FACE_COUNT] = {
  /* 1 SMILE */ {nullptr, 0, 0, "SMILE"},
  MO(F_GIDDY, 600, "GIDDY"), MO(F_ECSTATIC, 1800, "ECSTATIC"), MO(F_CHARMED, 2600, "CHARMED"),
  MO(F_SPARKLE, 1800, "SPARKLE"), MO(F_WINK, 700, "WINK"), MO(F_STARRY, 2400, "STARRY"),
  MO(F_LAUGH, 900, "LAUGH"), MO(F_EAGER, 1800, "EAGER"), MO(F_ANTICIP, 2000, "ANTICIPATION"),
  MO(F_POUT, 2000, "POUT"), MO(F_TEARY, 2200, "TEARY"), MO(F_CRYING, 2200, "CRYING"),
  MO(F_DISAPP, 3100, "DISAPPOINTED"), MO(F_HEARTBRK, 3900, "HEARTBROKEN"), MO(F_GRUMPY, 2500, "GRUMPY"),
  MO(F_FURIOUS, 1300, "FURIOUS"), MO(F_FUMING, 1200, "FUMING"), MO(F_ANNOYED, 1800, "ANNOYED"),
  MO(F_FLUSHED, 1200, "FLUSHED"), MO(F_QUESTION, 2000, "QUESTIONING"), MO(F_INVESTIG, 2400, "INVESTIGATING"),
  MO(F_THOUGHT, 2800, "THOUGHTFUL"), MO(F_CONFUSED, 1400, "CONFUSED"), MO(F_WONDER, 2500, "WONDER"),
  MO(F_PLAYFUL, 1400, "PLAYFUL"), MO(F_MISCHIEF, 1200, "MISCHIEVOUS"), MO(F_CHEEKY, 700, "CHEEKY"),
  MO(F_DERP, 900, "DERP"), MO(F_LOVING, 2800, "LOVING"), MO(F_TIRED, 3200, "TIRED"),
  MO(F_SLEEPING, 700, "SLEEPING"), MO(K_STRETCH, 3100, "YAWNING"), MO(F_SLEEPY, 2000, "SLEEPY"),
  MO(F_SCARED, 1500, "SCARED"), MO(F_BORED, LOOPS, "BORED"), MO(F_EMBARR, 1900, "EMBARRASSED"),
  MO(F_HUNGRY, 3000, "HUNGRY"),
  MO(F_LISTENING, LOOPS, "LISTENING"), MO(F_THINKING, LOOPS, "THINKING"), MO(F_COMFORT, LOOPS, "COMFORT"),
  MO(F_BREATHE, LOOPS, "BREATHE"), MO(F_STORY, LOOPS, "STORYTELLER"),
  /* 44 CHARGING */ {nullptr, 0, 0, "CHARGING"},
  MO(K_NOD, 850, "LOW_BATTERY"), MO(F_UPSIDE, LOOPS, "UPSIDE_DOWN"), MO(F_OFFLINE, 4000, "OFFLINE_SORRY"),
  MO(F_UPDATING, 2000, "UPDATING"),
};

// ======================== local reaction motions (§7.5) ========================
static const Kf L_HI[] = {
  M_EYE(0, 0, -8, 0), M_OSC(0, 400, J_HP, E_EIO, 8, -8, 100), M_MV(400, 500, J_HP, E_EIO, 0, 0, 0),
  M_MV(0, 150, J_ARMS, E_EO, 0, 22, 22), M_MV(150, 350, J_ARMS, E_EIO, 0, 10, 10), M_EYE(700, 0, 0, 0),
};
static const Kf L_HEART_[] = {
  M_EYE(0, 0, 4, 0), M_MV(0, 1200, J_ARMS, E_EIO, 0, 50, 50),
  M_ARMS_REST(2800, 4000), M_EYE(3600, 0, 0, 0),
};
static const Kf L_PURR_[] = {
  M_ARMS_REST(0, 500), M_OSC(0, 6000, J_HP, E_EIO, 5, -5, 833), M_MV(6000, 6400, J_HP, E_EIO, 0, 0, 0),
};
static const Kf L_SHIMMY_[] = {
  M_OSC(0, 900, J_HP, E_EIO, 8, -8, 110), M_OSC(0, 900, J_ARMS, E_EIO | E_ANTI, 20, 40, 110),
  M_REST(900, 1200),
};
static const Kf L_GLANCE_BACK_[] = {
  M_MV(0, 400, J_HP, E_EIO, 45, 0, 0), M_EYE(0, 18, 0, 0),
  M_MV(1400, 1800, J_HP, E_EIO, 0, 0, 0), M_EYE(1400, 0, 0, 0),
};
static const Kf L_TURN_BACK_[] = {
  M_MV(0, 400, J_HP, E_EIO, 60, 0, 0), M_EYE(0, 20, 0, 0),
  M_MV(2000, 2500, J_HP, E_EIO, 0, 0, 0), M_EYE(2000, 0, 0, 0),
};
static const Kf L_ARM_SHIMMY_[] = {
  M_OSC(0, 1000, J_HP, E_EIO, 12, -12, 125), M_OSC(0, 1000, J_ARMS, E_EIO | E_ANTI, 15, 45, 125),
  M_REST(1000, 1300),
};
static const Kf L_HAND_HOLD_[] = {M_MV(0, 400, J_HP, E_EIO, 25, 0, 0), M_EYE(0, 10, 4, 0), M_LOOP(600, 500)};
static const Kf L_PICKUP_[] = {M_MV(0, 300, J_ARMS, E_EO, 0, 40, 40), M_LOOP(500, 400)};
static const Kf L_FALL_[] = {M_MV(0, 120, J_ARMS, E_EO, 0, 0, 0), M_LOOP(300, 200)};
static const Kf L_HUG_LIFTED_[] = {
  M_TORQ(0, J_ARMS, TP_GENTLE), M_MV(0, 600, J_ALL, E_EIO, 0, 85, 85), M_EYE(0, 0, 4, 0),
  M_LOOP(800, 700),
};
static const Kf L_GLANCE_[] = {
  M_MV(0, 400, J_HP, E_EIO, 10, 0, 0), M_EYE(0, 10, 0, 0),
  M_MV(1400, 1800, J_HP, E_EIO, 0, 0, 0), M_EYE(1400, 0, 0, 0),
};
static const Kf L_APPROACH_[] = {
  M_MV(0, 500, J_HP, E_EIO, 60, 0, 0), M_EYE(0, 20, 0, 0),
  M_MV(2500, 3000, J_HP, E_EIO, 0, 0, 0), M_EYE(2500, 0, 0, 0),
};
static const Kf L_DIZZY_[] = {M_TRM(0, 3000, J_HP, 15, 30, 1)};
static const Kf L_SCAN_[] = {
  M_MV(0, 1000, J_HP, E_EIO, 40, 0, 0), M_MV(1000, 3000, J_HP, E_EIO, -40, 0, 0),
  M_MV(3000, 4000, J_HP, E_EIO, 0, 0, 0),
};
static const Kf L_CLAP1_[] = {
  M_MV(0, 160, J_ARMS, E_EO, 0, 95, 95), M_SND(160, S_CLAP), M_MV(160, 320, J_ARMS, E_EIO, 0, 60, 60),
};
static const Motion LOCALS[L_COUNT] = {
  MO(L_HI, 800, "hi_perk"), MO(L_HEART_, 4000, "heart"), MO(L_PURR_, 6400, "purr"),
  MO(L_SHIMMY_, 1200, "shimmy"), MO(L_GLANCE_BACK_, 1800, "glance_back"), MO(L_TURN_BACK_, 2500, "turn_back"),
  MO(L_ARM_SHIMMY_, 1300, "arm_shimmy"), MO(L_HAND_HOLD_, LOOPS, "hand_hold"), MO(L_PICKUP_, LOOPS, "pickup"),
  MO(L_FALL_, LOOPS, "fall"), MO(L_HUG_LIFTED_, LOOPS, "hug_lifted"), MO(L_GLANCE_, 1800, "glance"),
  MO(L_APPROACH_, 3000, "approach"), MO(L_DIZZY_, 3000, "dizzy"), MO(L_SCAN_, 4000, "scan"),
  MO(L_CLAP1_, 320, "clap1"),
};

int gestureFromName(const char* name) {
  for (int i = 0; i < G_COUNT; i++) if (strcmp(GESTURE_NAMES[i], name) == 0) return i;
  return -1;
}

bool motionMirrored(uint16_t id) {
  return id == G_WAVE_LEFT || id == G_HIGH_FIVE_LEFT || id == G_POINT_RIGHT || id == G_LOOK_RIGHT;
}

const Motion* motionGet(uint16_t id) {
  const Motion* m = nullptr;
  if (id < G_COUNT) m = &GESTURES[id];
  else if (id < G_COUNT + FACE_COUNT) m = &FACE_MOTIONS[id - G_COUNT];
  else if (id < MOT_COUNT) m = &LOCALS[id - G_COUNT - FACE_COUNT];
  return (m && m->kf) ? m : nullptr;
}

// ================================ dances (§7.6) ================================
uint8_t motionDance(uint8_t p, uint16_t bpm, uint8_t bars, Kf* o, uint8_t max) {
  if (bpm < 40) bpm = 100;
  uint16_t B = 60000 / bpm, h = B / 2, beats = bars * 4;
  uint8_t n = 0;
  auto add = [&](Kf k) { if (n < max - 1) o[n++] = k; };   // keep one slot for the final REST
  for (uint16_t k = 0; k < beats; k++) {
    uint16_t t = k * B;
    uint8_t q = k % 4;
    switch (p) {
      case 1:    // pump
        add(M_MV(t, t + h, J_ARMS, E_EO, 0, (k & 1) ? 10 : 145, (k & 1) ? 145 : 10));
        add(M_EYE(t, 0, -3, 0)); add(M_EYE(t + h, 0, 0, 0));
        break;
      case 2:    // flap
        add(M_MV(t, t + h, J_ARMS, E_EIO, 0, 90, 90)); add(M_MV(t + h, t + B, J_ARMS, E_EIO, 0, 30, 30));
        break;
      case 3:    // sway
        if (q == 0) add(M_MV(t, t + 2 * B, J_ALL, E_EIO, 25, 10, 60));
        if (q == 2) add(M_MV(t, t + 2 * B, J_ALL, E_EIO, -25, 60, 10));
        break;
      case 4:    // head bop
        add(M_MV(t, t + h, J_HP, E_EO, (k & 1) ? -10 : 10, 0, 0));
        add(M_MV(t, t + B, J_ARMS, E_EIO, 0, (k & 1) ? 20 : 35, (k & 1) ? 20 : 35));
        add(M_EYE(t, 0, -3, 0)); add(M_EYE(t + h, 0, 0, 0));
        break;
      case 5: {  // disco
        static const int8_t HPq[4] = {25, 0, -25, 0};
        static const uint8_t ALq[4] = {140, 10, 10, 10}, ARq[4] = {10, 10, 140, 10};
        add(M_MV(t, t + h, J_ALL, E_EO, HPq[q], ALq[q], ARq[q]));
        break;
      }
      case 6: {  // robot: staccato, held to the beat
        static const uint8_t ALq[4] = {90, 10, 90, 10}, ARq[4] = {10, 90, 90, 10};
        add(M_MV(t, t + 120, J_ARMS, E_EO, 0, ALq[q], ARq[q]));
        break;
      }
      case 7: {  // clap on 2 and 4
        uint16_t c = min<uint16_t>(160, h);
        if (q == 0) add(M_MV(t, t + h, J_ARMS, E_EIO, 0, 60, 60));
        if (q == 1 || q == 3) {
          add(M_MV(t, t + c, J_ARMS, E_EO, 0, 95, 95)); add(M_SND(t + c, S_CLAP));
          add(M_MV(t + c, t + B, J_ARMS, E_EIO, 0, 60, 60));
        }
        break;
      }
      case 8:    // hands up
        if (q == 0) add(M_MV(t, t + h, J_ARMS, E_EO, 0, 145, 145));
        if (q == 2) add(M_MV(t, t + B, J_ARMS, E_EIO, 0, 30, 30));
        break;
      case 9:    // wave hands: 180 deg apart, period 2 beats
        if (k % 2 == 0) {
          add(M_OSC(t, t + 2 * B, J_ARMS, E_EIO | E_ANTI, 120, 40, B));
          add(M_OSC(t, t + 2 * B, J_HP, E_EIO, 10, -10, B));
        }
        break;
      case 10:   // spin without wheels: HP sweep +60 -> -60 over 2 bars + dance_9's arms
        if (k % 8 == 0) {
          add(M_MV(t, t + 4 * B, J_HP, E_EIO, 60, 0, 0));
          add(M_MV(t + 4 * B, t + 8 * B, J_HP, E_EIO, -60, 0, 0));
        }
        if (k % 2 == 0) add(M_OSC(t, t + 2 * B, J_ARMS, E_EIO | E_ANTI, 120, 40, B));
        break;
    }
  }
  o[n++] = M_REST((uint16_t)(beats * B), (uint16_t)(beats * B + 400));
  return n;
}
