#include "Console.h"
#include "../State.h"
#include "../store/Store.h"
#include "../face/Face.h"
#include "../face/Glow.h"
#include "../audio/Audio.h"
#include "../audio/Sfx.h"
#include "../audio/Turn.h"
#include "../sense/Sense.h"
#include "../body/Body.h"
#include "../net/Net.h"
#include "../behavior/Behavior.h"
#include "../../Contract.h"
#include "esp_ota_ops.h"

static const char HELP[] =
  "face N (1-48) | sound N (1-46) | sound boot | clip N|name | phrase ID\n"
  "gesture NAME | pose HP AL AR [ms] | gaze X Y | dance N [BPM] | mode idle|play|story|quiet|sleep\n"
  "servo scan | servo ID ping|pos|load|volt|torque on|off|setid NEW|maxtorque PCT\n"
  "drive V_mms W_mrads DIST_mm | stop | bench on|off (wheels in the air)\n"
  "glow ears|cheeks|belly #RRGGBB solid|pulse|rainbow PCT\n"
  "rail | touch raw | imu raw | tof raw | mic level | (repeat to stop a dump)\n"
  "cal show|save|reset | cal set KNOB VALUES.. | cal servo N center | cal imu | cal tof | cal rail\n"
  "wifi SSID PASS (\"quote\" one with spaces) | id DEVICE_ID TOKEN | url PC_IP | url MQTT_URL AUDIO_URL   (saved, then reboot)\n"
  "set movement on|off | set volume N | set quiet HH:MM HH:MM | set quiet off\n"
  "fw | net | state | faults | selftest wire | selftest load | turn test | reboot\n";

static bool micDump = false;
static uint32_t loadUntil = 0;

static void reboot() {
  Serial.println("saved - rebooting");
  delay(300);
  ESP.restart();
}

// One argument: a bare word, or "a quoted run" that may hold spaces (home SSIDs often do). Advances p.
static char* nextArg(char*& p) {
  while (*p == ' ') p++;
  if (!*p) return nullptr;
  char end = ' ';
  if (*p == '"') { end = '"'; p++; }
  char* start = p;
  while (*p && *p != end) p++;
  if (*p) *p++ = '\0';
  return start;
}

static void run(char* line, uint32_t now) {
  if (!strncmp(line, "wifi ", 5)) {   // its own split: strtok below would cut "My Home-2.4G" in two
    char* p = line + 5;
    char* ssid = nextArg(p);
    char* pass = nextArg(p);
    if (ssid && *ssid) {
      strlcpy(settings.wifiSsid, ssid, sizeof settings.wifiSsid);
      strlcpy(settings.wifiPass, pass ? pass : "", sizeof settings.wifiPass);
      settingsSave(); reboot();
    }
  }
  char* save;
  char* cmd = strtok_r(line, " ", &save);
  if (!cmd) return;
  char* a = strtok_r(nullptr, " ", &save);
  char* b = strtok_r(nullptr, " ", &save);
  char* c = strtok_r(nullptr, " ", &save);
  char* d = strtok_r(nullptr, " ", &save);
  int n = a ? atoi(a) : 0;

  if (!strcmp(cmd, "help")) { Serial.print(HELP); return; }
  if (!strcmp(cmd, "face") && n >= 1 && n <= FACE_COUNT) { behaviorFace((FaceId)(n - 1)); return; }
  if (!strcmp(cmd, "sound") && a) {
    if (!strcmp(a, "boot")) sfxPlay(S_BOOT);
    else if (n >= 1 && n <= SND_COUNT) { sfxPlay(n - 1); Serial.printf("sound %d %s\n", n, SOUND_NAMES[n - 1]); }
    return;
  }
  if (!strcmp(cmd, "clip") && a) {
    for (int i = 0; i < CLIP_COUNT; i++)
      if (n == i + 1 || !strcmp(a, CLIP_NAMES[i])) { sfxPlayClip((ClipId)i); Serial.printf("clip %s (prototype only: licence unknown)\n", CLIP_NAMES[i]); }
    return;
  }
  if (!strcmp(cmd, "phrase")) { Serial.println("phrase pack: needs the SD player (not on this build); reactions use the SFX fallback"); return; }
  if (!strcmp(cmd, "gesture") && a) {
    int g = gestureFromName(a);
    if (g < 0) { Serial.print("gestures:"); for (int i = 0; i < G_COUNT; i++) Serial.printf(" %s", GESTURE_NAMES[i]); Serial.println(); }
    else behaviorGesture(g);
    return;
  }
  if (!strcmp(cmd, "pose") && c) { bodyPose(atoi(a), atoi(b), atoi(c), d ? atoi(d) : 800, P_LOCAL); return; }
  if (!strcmp(cmd, "gaze") && b) { faceGaze(atoi(a), atoi(b)); return; }
  if (!strcmp(cmd, "dance") && n >= 1 && n <= 10) { behaviorDance(n, b ? atoi(b) : 100); return; }
  if (!strcmp(cmd, "mode") && a) {
    int m = contractIndex(MODES, a);
    if (m >= 0) behaviorSetMode(m);
    return;
  }
  if (!strcmp(cmd, "servo") || !strcmp(cmd, "rail")) {
    char full[64];
    snprintf(full, sizeof full, "%s %s %s %s", cmd, a ? a : "", b ? b : "", c ? c : "");
    bodyConsole(full);
    if (!strcmp(cmd, "rail")) Serial.printf("battery estimate %u%%, low-power %s\n", S.batteryPct, S.lowPower ? "ON" : "off");
    return;
  }
  if (!strcmp(cmd, "drive") && c) {   // bench: wheels off the ground first, and `bench on`
    int v = atoi(a), w = atoi(b), dist = atoi(c);
    if (v) bodyDrive(DM_LINEAR, v, 0, dist, false, 0);
    else bodyDrive(DM_TURN, 0, w, dist, false, 0);
    return;
  }
  if (!strcmp(cmd, "stop")) { bodyDrive(DM_STOP, 0, 0, 0, false, 0); return; }
  if (!strcmp(cmd, "bench") && a) { bodyBench(!strcmp(a, "on")); return; }
  if (!strcmp(cmd, "glow") && d) {
    int t = contractIndex(LED_TARGETS, a), fx = contractIndex(LED_EFFECTS, c);
    if (t < 0 || fx < 0 || b[0] != '#') { Serial.println("glow ears|cheeks|belly #RRGGBB solid|pulse|rainbow PCT"); return; }
    glowCloud(t, strtoul(b + 1, nullptr, 16), fx, constrain(atoi(d), 0, 100));
    return;
  }
  if ((!strcmp(cmd, "touch") || !strcmp(cmd, "imu") || !strcmp(cmd, "tof")) && a && !strcmp(a, "raw")) {
    static SenseDump cur = DUMP_OFF;
    SenseDump want = cmd[0] == 't' && cmd[1] == 'o' && cmd[2] == 'u' ? DUMP_TOUCH : cmd[0] == 'i' ? DUMP_IMU : DUMP_TOF;
    cur = cur == want ? DUMP_OFF : want;
    senseDump(cur);
    return;
  }
  if (!strcmp(cmd, "mic")) { micDump = !micDump; return; }
  if (!strcmp(cmd, "cal") && a) {
    if (!strcmp(a, "show")) calShow(Serial);
    else if (!strcmp(a, "save")) calSave();
    else if (!strcmp(a, "reset")) calReset();
    else if (!strcmp(a, "imu")) senseCalImu();
    else if (!strcmp(a, "tof")) senseCalTof();
    else if (!strcmp(a, "servo")) {
      char full[48];
      snprintf(full, sizeof full, "cal servo %s %s", b ? b : "", c ? c : "");
      bodyConsole(full);
    } else if (!strcmp(a, "rail")) {   // stage 0 (b): the rail is starting to sag now = "empty"
      if (!S.railMv) { Serial.println("no rail reading (no servo answering)"); return; }
      cal.railEmptyMv = S.railMv;
      Serial.printf("railEmptyMv = %u (cal save to keep)\n", cal.railEmptyMv);
    } else if (!strcmp(a, "set") && b && c) {
      char vals[64] = "";
      strlcat(vals, c, sizeof vals);
      if (d) { strlcat(vals, " ", sizeof vals); strlcat(vals, d, sizeof vals); }
      if (save && *save) { strlcat(vals, " ", sizeof vals); strlcat(vals, save, sizeof vals); }
      Serial.println(calSet(b, vals) ? "ok (cal save to keep; some knobs apply after a reboot)" : "unknown knob or bad value");
    }
    return;
  }
  if (!strcmp(cmd, "id") && b) {
    strlcpy(settings.deviceId, a, sizeof settings.deviceId);
    strlcpy(settings.token, b, sizeof settings.token);
    settingsSave(); reboot();
  }
  if (!strcmp(cmd, "url") && a) {
    if (!settingsServer(a, b ? b : "")) { Serial.println("url PC_IP   or   url mqtt://HOST:PORT ws://HOST:PORT"); return; }
    settingsSave(); reboot();
  }
  if (!strcmp(cmd, "set") && a && b) {
    if (!strcmp(a, "movement")) settings.movementOn = !strcmp(b, "on");
    else if (!strcmp(a, "volume")) settings.volume = constrain(atoi(b), 0, 100);
    else if (!strcmp(a, "quiet")) {
      if (!strcmp(b, "off")) settings.quietStart[0] = settings.quietEnd[0] = 0;
      else if (c) { strlcpy(settings.quietStart, b, 6); strlcpy(settings.quietEnd, c, 6); }
    }
    settingsSave();
    Serial.printf("movement %s, volume %u, quiet %s-%s\n", settings.movementOn ? "on" : "off", settings.volume,
                  settings.quietStart, settings.quietEnd);
    return;
  }
  if (!strcmp(cmd, "fw")) {
    const esp_partition_t* p = esp_ota_get_running_partition();
    Serial.printf("device_id %s  fw_ver %s  proto_ver %d  FW-SOT %s  pairing %s  token %s\n", settings.deviceId, FW_VER,
                  PROTO_VER, FW_SOT_VER, netPairingCode(), settings.token[0] ? "set" : "not set");
    Serial.printf("partition %s  heap %u (min %u)  PSRAM %u free of %u  reset %d  uptime %lus\n", p ? p->label : "?",
                  ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getFreePsram(), ESP.getPsramSize(), esp_reset_reason(),
                  (unsigned long)(now / 1000));
    return;
  }
  if (!strcmp(cmd, "net")) { netStatus(Serial); return; }
  if (!strcmp(cmd, "state")) { behaviorPrintState(Serial); return; }
  if (!strcmp(cmd, "faults")) {
    uint32_t m = faultMask();
    Serial.print("faults:");
    for (int f = 0; f < F_COUNT; f++) if (m & (1u << f)) Serial.printf(" %s", FAULT_NAMES[f]);
    Serial.println(m ? "" : " none");
    return;
  }
  if (!strcmp(cmd, "selftest") && a) {
    if (!strcmp(a, "wire")) netSelfTest(Serial);
    else if (!strcmp(a, "load")) {   // stage 0 (a): wheels + a big arm move + loud audio, all at once
      Serial.println("selftest load: wheels (in the air) + STRETCH + loud sound for 4 s; watch `rail` and the multimeter");
      bodyBench(true);
      bodyDrive(DM_LINEAR, 250, 0, 900, false, 0);
      bodyMotion(G_STRETCH, P_SAFETY);
      audioVolume(100);
      sfxPlay(FACE_ECSTATIC, 0, 1);
      loadUntil = now + 4000;
    }
    return;
  }
  if (!strcmp(cmd, "turn")) { turnTest(Serial); return; }
  if (!strcmp(cmd, "reboot")) reboot();
  if (n >= 1 && n <= FACE_COUNT && !a) { behaviorFace((FaceId)(n - 1)); return; }   // Eillik habit: just a number
  Serial.println("? (help)");
}

void consoleLoop(uint32_t now) {
  static char buf[160];
  static uint8_t len = 0;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      buf[len] = 0;
      len = 0;
      if (buf[0] >= '0' && buf[0] <= '9') {   // "12" alone = face 12
        int n = atoi(buf);
        if (n >= 1 && n <= FACE_COUNT) { behaviorFace((FaceId)(n - 1)); continue; }
      }
      run(buf, now);
    } else if (len < sizeof buf - 1) buf[len++] = ch;
  }
  static uint32_t micAt = 0, loadAt = 0;
  if (micDump && now - micAt >= 100) {
    micAt = now;
    float l, r;
    audioMicLevels(l, r);
    Serial.printf("mic,%lu,%.1f,%.1f\n", (unsigned long)now, l, r);
  }
  if (loadUntil) {
    if (now - loadAt >= 250) { loadAt = now; Serial.printf("load,%lu,rail,%u\n", (unsigned long)now, S.railMv); }
    if ((int32_t)(now - loadUntil) >= 0) {
      loadUntil = 0;
      sfxStop();
      bodyDrive(DM_STOP, 0, 0, 0, false, 0);
      bodyBench(false);
      audioVolume(settings.volume);
      Serial.println("selftest load: done. Pass = rail >= 4.9 V and no reset (FW-SOT §15 stage 0)");
    }
  }
}
