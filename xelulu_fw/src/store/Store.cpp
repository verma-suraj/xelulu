#include "Store.h"
#include <Preferences.h>
#include <stddef.h>

Settings settings;
Cal cal;

static Preferences nvs;
static SemaphoreHandle_t lock;   // Preferences is one shared handle used from several tasks

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
static void getStr(const char* key, char* dst, size_t n, const char* def) {
  if (nvs.isKey(key)) nvs.getString(key, dst, n);
  else strlcpy(dst, def, n);
}

bool settingsServer(const char* mqtt, const char* audio) {
  char m[sizeof settings.mqttUrl], a[sizeof settings.audioUrl];
  if (!strstr(mqtt, "://")) {
    if (!*mqtt) return false;
    for (const char* p = mqtt; *p; p++) if (!isalnum((uint8_t)*p) && *p != '.' && *p != '-') return false;
    if (snprintf(m, sizeof m, "mqtt://%s:1883", mqtt) >= (int)sizeof m) return false;
    snprintf(a, sizeof a, "ws://%s:8081", mqtt);   // two characters shorter than m: fits
  } else {
    if ((strncmp(mqtt, "mqtt://", 7) && strncmp(mqtt, "mqtts://", 8)) || strlen(mqtt) >= sizeof m) return false;
    if ((*audio && strncmp(audio, "ws://", 5) && strncmp(audio, "wss://", 6)) || strlen(audio) >= sizeof a) return false;
    strcpy(m, mqtt);
    strcpy(a, audio);                                // "" = no voice turns
  }
  strcpy(settings.mqttUrl, m);
  strcpy(settings.audioUrl, a);
  return true;
}

// Config.h's NET_* values are applied once per change (an FNV-1a of all five is kept in NVS): a reflash
// with new values overwrites what the robot saved, the same build again keeps later console / web edits.
static void applyNetDefaults() {
  static_assert(sizeof NET_WIFI_SSID <= sizeof(Settings::wifiSsid), "Config.h NET_WIFI_SSID: 32 characters at most");
  static_assert(sizeof NET_WIFI_PASS <= sizeof(Settings::wifiPass) - 1, "Config.h NET_WIFI_PASS: 63 characters at most");
  static_assert(sizeof NET_DEVICE_ID <= sizeof(Settings::deviceId), "Config.h NET_DEVICE_ID: 39 characters at most");
  static_assert(sizeof NET_DEVICE_TOKEN <= sizeof(Settings::token), "Config.h NET_DEVICE_TOKEN: 95 characters at most");
  uint32_t h = 2166136261u;
  for (const char* v : {NET_WIFI_SSID, NET_WIFI_PASS, NET_SERVER, NET_DEVICE_ID, NET_DEVICE_TOKEN})
    for (const char* p = v;; p++) { h = (h ^ (uint8_t)*p) * 16777619u; if (!*p) break; }   // the NULs separate them
  if (nvs.getULong("netdef", 0) == h) return;
  if (NET_WIFI_SSID[0]) {
    strlcpy(settings.wifiSsid, NET_WIFI_SSID, sizeof settings.wifiSsid);
    strlcpy(settings.wifiPass, NET_WIFI_PASS, sizeof settings.wifiPass);
  }
  if (NET_SERVER[0] && !settingsServer(NET_SERVER, "")) Serial.println("[net] Config.h NET_SERVER is not a bare IP or host name: ignored");
  if (NET_DEVICE_ID[0]) strlcpy(settings.deviceId, NET_DEVICE_ID, sizeof settings.deviceId);
  if (NET_DEVICE_TOKEN[0]) strlcpy(settings.token, NET_DEVICE_TOKEN, sizeof settings.token);
  settingsSave();
  nvs.putULong("netdef", h);
  Serial.println("[net] Config.h network values applied");
}

void storeBegin() {
  lock = xSemaphoreCreateMutex();
  nvs.begin("xlu", false);

  getStr("id", settings.deviceId, sizeof settings.deviceId, "xlu_000123");   // dev value (§4.1)
  getStr("tok", settings.token, sizeof settings.token, "");
  getStr("ssid", settings.wifiSsid, sizeof settings.wifiSsid, "");
  getStr("pass", settings.wifiPass, sizeof settings.wifiPass, "");
  getStr("mqtt", settings.mqttUrl, sizeof settings.mqttUrl, "");
  getStr("audio", settings.audioUrl, sizeof settings.audioUrl, "");
  settings.volume = nvs.getUChar("vol", 100);   // full until the app sets a level (bench 2026-10-31, §10.4)
  getStr("qs", settings.quietStart, sizeof settings.quietStart, "");
  getStr("qe", settings.quietEnd, sizeof settings.quietEnd, "");
  // ponytail: wheels stay off until a parent's config (or the console) turns them on
  settings.movementOn = nvs.getBool("mov", false);
  getStr("lang", settings.language, sizeof settings.language, "en-IN");
  getStr("child", settings.childId, sizeof settings.childId, "");
  settings.childSet = nvs.getBool("childset", false);
  settings.mode = nvs.getUChar("mode", 0);
  applyNetDefaults();

  Cal stored;
  if (nvs.getBytesLength("cal") == sizeof(Cal) &&
      nvs.getBytes("cal", &stored, sizeof(Cal)) == sizeof(Cal) && stored.version == CAL_VERSION) {
    cal = stored;
  } else {
    cal = Cal{};
    Serial.println("[cal] no valid blob in NVS: using Config.h defaults");
  }
}

void settingsSave() {
  xSemaphoreTake(lock, portMAX_DELAY);
  nvs.putString("id", settings.deviceId);
  nvs.putString("tok", settings.token);
  nvs.putString("ssid", settings.wifiSsid);
  nvs.putString("pass", settings.wifiPass);
  nvs.putString("mqtt", settings.mqttUrl);
  nvs.putString("audio", settings.audioUrl);
  nvs.putUChar("vol", settings.volume);
  nvs.putString("qs", settings.quietStart);
  nvs.putString("qe", settings.quietEnd);
  nvs.putBool("mov", settings.movementOn);
  nvs.putString("lang", settings.language);
  nvs.putString("child", settings.childId);
  nvs.putBool("childset", settings.childSet);
  nvs.putUChar("mode", settings.mode);
  xSemaphoreGive(lock);
}

// ---------------------------------------------------------------------------
// Calibration blob: one descriptor per knob drives both `cal show` and `cal set`
// ---------------------------------------------------------------------------
enum Kind : uint8_t { K_U8, K_I8, K_U16, K_I16, K_F32 };
template <class T> struct KindOf;
template <> struct KindOf<uint8_t>  { static constexpr uint8_t k = K_U8,  n = 1; };
template <> struct KindOf<int8_t>   { static constexpr uint8_t k = K_I8,  n = 1; };
template <> struct KindOf<uint16_t> { static constexpr uint8_t k = K_U16, n = 1; };
template <> struct KindOf<int16_t>  { static constexpr uint8_t k = K_I16, n = 1; };
template <> struct KindOf<float>    { static constexpr uint8_t k = K_F32, n = 1; };
template <class T, size_t N> struct KindOf<T[N]> { static constexpr uint8_t k = KindOf<T>::k, n = N; };

struct Knob { const char* name; uint16_t off; uint8_t kind, n; };
#define KN(f) { #f, (uint16_t)offsetof(Cal, f), KindOf<decltype(Cal::f)>::k, KindOf<decltype(Cal::f)>::n }
#define SECTION(t) { t, 0xFFFF, 0, 0 }

// Order = the Report-Back "Hardware facts" table (§14.4).
static const Knob KNOBS[] = {
  SECTION("Microphones (2x SPH0645, I2S0)"),
  KN(micGainDb), KN(aecRefDelayMs), KN(micSpacingMm), KN(micsOnHead), KN(micMsbFormat),
  SECTION("Speaker + amplifier (MAX98357A, 0.5 W 8 ohm)"),
  KN(volMaxDb), KN(sndGain), KN(driveDuckDb),
  SECTION("Touch (MPR121: head, back, belly + optional hands; no ears)"),
  KN(handsFitted), KN(touchEle), KN(touchThr), KN(relThr), KN(tapMinMs), KN(tapMaxMs),
  KN(doubleGapMs), KN(holdMs), KN(patWindowMs), KN(handGateMs),
  SECTION("LEDs (none: ears / cheeks / belly drawn on screen)"),
  KN(glowMode), KN(brightNormal), KN(brightDim), KN(brightQuiet), KN(brightSleep),
  KN(glowCapNormal), KN(glowCapQuiet), KN(glowCapSleep),
  SECTION("IMU (GY-87: MPU6050 used)"),
  KN(imuUp), KN(imuCalDone), KN(shakeG), KN(shakeWinMs), KN(shakeCoolMs), KN(knockG),
  KN(knockQuietMs), KN(dropG), KN(dropMs), KN(impactG), KN(upsideDeg), KN(upsideClearDeg),
  KN(upsideMs), KN(tiltDeg), KN(tiltClearDeg), KN(tiltMs), KN(tiltClearMs), KN(spinDps), KN(spinMs),
  SECTION("NFC reader: not fitted (G22)"),
  SECTION("Servos (3x SC09: 1 head pan, 2 left arm, 3 right arm)"),
  KN(jointFitted), KN(servoBusPins), KN(center), KN(dir), KN(softMin), KN(softMax),
  KN(speedCap), KN(accCap), KN(torqueNormal), KN(torqueGentle), KN(torqueCompliant),
  KN(grabPct), KN(grabMs), KN(fiveJumpPct), KN(overloadPct), KN(overloadMs), KN(overheatC),
  KN(overvoltMv),
  SECTION("Motors (2x N20 + encoders, TB6612FNG)"),
  KN(wheelDiamMm), KN(wheelBaseMm), KN(countsPerRev), KN(wheelDir), KN(kp), KN(ki), KN(vFwdMms),
  KN(vSlowMms), KN(vRevMms), KN(wTurn), KN(wSpin), KN(accLin), KN(accAng), KN(tetherMm),
  KN(stopFrontMm), KN(stopRearMm), KN(slowZoneMm),
  SECTION("Battery (2S LG MJ1, estimate from the servo-reported 5 V rail)"),
  KN(railOkMv), KN(railLowMv), KN(railCritMv), KN(railEmptyMv), KN(peakWindowMs), KN(bigArmDeg),
  SECTION("Charging: not sensed (charge with the switch off)"),
  SECTION("Anything else: ToF (4x VL53L0X via TCA9548A), sync, voice, idle, clock"),
  KN(tofCh), KN(tofLabelled), KN(floorMm), KN(cliffMarginMm), KN(liftMarginMm),
  KN(sigRateCentiMcps), KN(servoLeadMs), KN(faceLeadMs), KN(glowLeadMs), KN(opusComplexity),
  KN(opusKbps), KN(prerollMs), KN(vadTailMs), KN(pttMaxMs), KN(followupMs),
  KN(followupGrandmaMs), KN(jitterStartMs), KN(jitterMaxMs), KN(bargeinMarginDb), KN(cryConf),
  KN(cryMinS), KN(glanceMinS), KN(glanceMaxS), KN(fidgetMinS), KN(fidgetMaxS), KN(approachS),
  KN(windDownS), KN(windDownQuietS), KN(tzOffsetMin),
};

static const uint8_t KIND_SIZE[] = {1, 1, 2, 2, 4};

void calSave() {
  xSemaphoreTake(lock, portMAX_DELAY);
  cal.version = CAL_VERSION;
  nvs.putBytes("cal", &cal, sizeof(Cal));
  xSemaphoreGive(lock);
  Serial.println("[cal] saved");
}

void calReset() {
  cal = Cal{};
  xSemaphoreTake(lock, portMAX_DELAY);
  nvs.remove("cal");
  xSemaphoreGive(lock);
  Serial.println("[cal] reset to Config.h defaults (not saved until `cal save`)");
}

void calShow(Print& out) {
  out.printf("cal v%u (FW %s, FW-SOT %s)\n", cal.version, FW_VER, FW_SOT_VER);
  for (const Knob& k : KNOBS) {
    if (k.off == 0xFFFF) { out.printf("-- %s\n", k.name); continue; }
    out.printf("  %-18s =", k.name);
    const uint8_t* base = (const uint8_t*)&cal + k.off;
    for (uint8_t i = 0; i < k.n; i++) {
      const uint8_t* p = base + i * KIND_SIZE[k.kind];
      switch (k.kind) {
        case K_U8:  out.printf(" %u", *p); break;
        case K_I8:  out.printf(" %d", *(const int8_t*)p); break;
        case K_U16: out.printf(" %u", *(const uint16_t*)p); break;
        case K_I16: out.printf(" %d", *(const int16_t*)p); break;
        case K_F32: out.printf(" %.4g", *(const float*)p); break;
      }
    }
    out.println();
  }
}

bool calSet(const char* name, const char* values) {
  for (const Knob& k : KNOBS) {
    if (k.off == 0xFFFF || strcmp(k.name, name) != 0) continue;
    uint8_t* base = (uint8_t*)&cal + k.off;
    const char* s = values;
    for (uint8_t i = 0; i < k.n; i++) {
      char* end;
      if (k.kind == K_F32) {
        float v = strtof(s, &end);
        if (end == s) return i > 0;      // fewer values than elements: keep the rest
        memcpy(base + i * 4, &v, 4);
      } else {
        long v = strtol(s, &end, 0);
        if (end == s) return i > 0;
        static const long LO[] = {0, -128, 0, -32768}, HI[] = {255, 127, 65535, 32767};
        if (v < LO[k.kind] || v > HI[k.kind]) return false;
        uint8_t* p = base + i * KIND_SIZE[k.kind];
        if (KIND_SIZE[k.kind] == 1) *p = (uint8_t)v; else { int16_t w = (int16_t)v; memcpy(p, &w, 2); }
      }
      s = end;
    }
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Epoch, cry queue, brownout log
// ---------------------------------------------------------------------------
uint32_t storeEpochLoad() { return nvs.getULong("epoch", 0); }

void storeEpochSave(uint32_t epoch) {
  xSemaphoreTake(lock, portMAX_DELAY);
  nvs.putULong("epoch", epoch);
  xSemaphoreGive(lock);
}

static char cryq[CRYQ_MAX][CRYQ_SLOT];
static bool cryLoaded = false;

static void cryLoad() {
  if (cryLoaded) return;
  memset(cryq, 0, sizeof cryq);
  if (nvs.getBytesLength("cryq") == sizeof cryq) nvs.getBytes("cryq", cryq, sizeof cryq);
  cryLoaded = true;
}

int cryQueueLoad(char (*out)[CRYQ_SLOT], int max) {
  xSemaphoreTake(lock, portMAX_DELAY);
  cryLoad();
  int n = 0;
  for (int i = 0; i < CRYQ_MAX && n < max; i++)
    if (cryq[i][0]) memcpy(out[n++], cryq[i], CRYQ_SLOT);
  xSemaphoreGive(lock);
  return n;
}

void cryQueueAdd(const char* payload) {
  if (strlen(payload) >= CRYQ_SLOT) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  cryLoad();
  int slot = 0;
  for (int i = 0; i < CRYQ_MAX; i++) if (!cryq[i][0]) { slot = i; break; }
  // ponytail: full queue overwrites slot 0; 20 unsent cry alerts means the link is long dead
  strlcpy(cryq[slot], payload, CRYQ_SLOT);
  nvs.putBytes("cryq", cryq, sizeof cryq);
  xSemaphoreGive(lock);
}

void cryQueueRemove(const char* eventId) {
  xSemaphoreTake(lock, portMAX_DELAY);
  cryLoad();
  bool changed = false;
  for (int i = 0; i < CRYQ_MAX; i++)
    if (cryq[i][0] && strstr(cryq[i], eventId)) { cryq[i][0] = 0; changed = true; }
  if (changed) nvs.putBytes("cryq", cryq, sizeof cryq);
  xSemaphoreGive(lock);
}

void brownoutLog(uint32_t epoch) {
  uint32_t log[2] = {0, 0};
  xSemaphoreTake(lock, portMAX_DELAY);
  nvs.getBytes("bo", log, sizeof log);
  log[1] = log[0];
  log[0] = epoch;
  nvs.putBytes("bo", log, sizeof log);
  xSemaphoreGive(lock);
}

bool brownoutTwiceWithin(uint32_t seconds) {
  uint32_t log[2] = {0, 0};
  nvs.getBytes("bo", log, sizeof log);
  return log[0] && log[1] && log[0] - log[1] <= seconds;
}

void brownoutClear() {
  xSemaphoreTake(lock, portMAX_DELAY);
  nvs.remove("bo");
  xSemaphoreGive(lock);
}
