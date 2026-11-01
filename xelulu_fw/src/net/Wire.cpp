#include "Wire.h"
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>

// ---------------------------------------------------------------------------
// field checks, one per zod primitive (primitives.ts)
// ---------------------------------------------------------------------------
static bool isInt(JsonVariantConst v, long long lo, long long hi, long long& out) {
  if (v.is<long long>()) out = v.as<long long>();
  else if (v.is<double>() && !v.is<bool>()) {          // 2.0 is an int to JSON.parse too
    double d = v.as<double>();
    if (!isfinite(d) || d != floor(d)) return false;
    out = (long long)d;
  } else return false;
  return out >= lo && out <= hi;
}
static bool isNonNegNumber(JsonVariantConst v, float& out) {
  if (!v.is<double>() || v.is<bool>()) return false;
  double d = v.as<double>();
  if (!isfinite(d) || d < 0) return false;
  out = (float)d;
  return true;
}
static bool isStr(JsonVariantConst v) { return v.is<const char*>(); }
static bool isNonEmpty(JsonVariantConst v) { return isStr(v) && v.as<const char*>()[0]; }

// copies a string field; false if it does not fit (cmd_id, content_id, child id, url must be exact)
static bool copyExact(char* dst, size_t n, JsonVariantConst v) {
  const char* s = v.as<const char*>();
  if (strlen(s) >= n) return false;
  strcpy(dst, s);
  return true;
}
// open-vocabulary fields: a value longer than any known name can only be "unknown" anyway
static void copyClip(char* dst, size_t n, JsonVariantConst v) {
  strncpy(dst, v.as<const char*>(), n - 1);
  dst[n - 1] = 0;
}

static bool az(char c) { return c >= 'a' && c <= 'z'; }
static bool AZ(char c) { return c >= 'A' && c <= 'Z'; }
static bool d09(char c) { return c >= '0' && c <= '9'; }

// LANG_RE = ^[a-z]{2,3}(?:-[A-Z][a-z]{3})?(?:-(?:[A-Z]{2}|[0-9]{3}))?$
static bool isLang(const char* s) {
  int i = 0;
  while (az(s[i])) i++;
  if (i < 2 || i > 3) return false;
  const char* p = s + i;
  if (!*p) return true;
  if (*p++ != '-') return false;
  if (AZ(p[0]) && az(p[1]) && az(p[2]) && az(p[3]) && (!p[4] || p[4] == '-')) {
    p += 4;
    if (!*p) return true;
    p++;
  }
  if (AZ(p[0]) && AZ(p[1]) && !p[2]) return true;
  return d09(p[0]) && d09(p[1]) && d09(p[2]) && !p[3];
}
// HHMM_RE = ^(?:[01][0-9]|2[0-3]):[0-5][0-9]$
static bool isHHMM(const char* s) {
  if (strlen(s) != 5 || s[2] != ':') return false;
  bool h = (s[0] == '0' || s[0] == '1') ? d09(s[1]) : (s[0] == '2' && s[1] >= '0' && s[1] <= '3');
  return h && s[3] >= '0' && s[3] <= '5' && d09(s[4]);
}
// COLOR_RE = ^#[0-9A-Fa-f]{6}$
static bool isColor(const char* s, uint32_t& rgb) {
  if (strlen(s) != 7 || s[0] != '#') return false;
  for (int i = 1; i < 7; i++) if (!isxdigit((unsigned char)s[i])) return false;
  rgb = strtoul(s + 1, nullptr, 16);
  return true;
}
// isAbsoluteHttpsUrl: "https://" + a non-empty host, nothing URL.parse would refuse
static bool isHttpsUrl(const char* s) {
  if (strncmp(s, "https://", 8) != 0) return false;
  for (const char* p = s; *p; p++) if ((unsigned char)*p <= ' ' || *p == '\\') return false;
  const char* host = s + 8;
  const char* end = host + strcspn(host, "/?#");
  const char* at = (const char*)memchr(host, '@', end - host);
  if (at) host = at + 1;
  size_t hostLen = strcspn(host, ":/?#");
  return hostLen > 0 && host < end;
}

// Strict object: every key must be in `allowed`; `present` gets one bit per allowed key.
static bool strictKeys(JsonObjectConst o, const char* const* allowed, uint8_t n, uint32_t& present) {
  present = 0;
  for (JsonPairConst kv : o) {
    int idx = -1;
    for (uint8_t i = 0; i < n; i++) if (strcmp(kv.key().c_str(), allowed[i]) == 0) { idx = i; break; }
    if (idx < 0) return false;                     // unrecognized_keys
    present |= 1u << idx;
  }
  return true;
}

#define FAIL(w) do { if (why) *why = (w); return PARSE_INVALID; } while (0)

// ---------------------------------------------------------------------------
ParseResult wireParseCmd(const char* json, size_t len, Cmd& c, const char** why) {
  memset(&c, 0, sizeof c);
  c.action = A_UNKNOWN;
  JsonDocument doc;
  if (deserializeJson(doc, json, len) || !doc.is<JsonObjectConst>()) { if (why) *why = "not a JSON object"; return PARSE_NOT_JSON; }
  JsonObjectConst o = doc.as<JsonObjectConst>();

  // envelope (C2): exactly proto_ver, cmd_id, action, data. No ts, no type.
  static const char* const ENV[] = {"proto_ver", "cmd_id", "action", "data"};
  uint32_t has;
  long long pv;
  bool keysOk = strictKeys(o, ENV, 4, has);
  if (!(has & 1) || !isInt(o["proto_ver"], 2, 2, pv)) { if (why) *why = "proto_ver"; return PARSE_PROTO; }
  if (!isNonEmpty(o["cmd_id"])) FAIL("cmd_id");
  if (!copyExact(c.cmdId, sizeof c.cmdId, o["cmd_id"])) FAIL("cmd_id longer than 63");
  if (!keysOk) FAIL("unknown envelope key");
  if (!isStr(o["action"])) FAIL("action");
  JsonVariantConst dv = o["data"];
  if (!(has & 8) || !dv.is<JsonObjectConst>()) FAIL("data");
  int a = contractIndex(CMD_ACTIONS, o["action"].as<const char*>());
  if (a < 0) return PARSE_UNKNOWN_ACTION;
  c.action = (uint8_t)a;
  JsonObjectConst d = dv.as<JsonObjectConst>();
  long long iv;
  int e;

  switch (c.action) {
    case A_SAY: {
      static const char* const K[] = {"expression", "lang", "voice", "duration_hint_ms"};
      if (!strictKeys(d, K, 4, has) || has != 0xF) FAIL("say keys");
      if (!isNonEmpty(d["expression"])) FAIL("say.expression");
      if (!isStr(d["lang"]) || !isLang(d["lang"].as<const char*>())) FAIL("say.lang");
      if (!isStr(d["voice"]) || (e = contractIndex(SAY_VOICES, d["voice"].as<const char*>())) < 0) FAIL("say.voice");
      if (!isInt(d["duration_hint_ms"], 0, 9007199254740991LL, iv)) FAIL("say.duration_hint_ms");
      copyClip(c.expression, sizeof c.expression, d["expression"]);
      strcpy(c.lang, d["lang"].as<const char*>());
      c.voice = e;
      c.durationHintMs = iv > 0xFFFFFFFFLL ? 0xFFFFFFFF : (uint32_t)iv;
      break;
    }
    case A_ANIMATE: {
      static const char* const K[] = {"expression", "eyes", "loop"};
      if (!strictKeys(d, K, 3, has) || has != 0x7) FAIL("animate keys");
      if (!isNonEmpty(d["expression"]) || !isNonEmpty(d["eyes"]) || !d["loop"].is<bool>()) FAIL("animate field");
      copyClip(c.expression, sizeof c.expression, d["expression"]);
      copyClip(c.eyes, sizeof c.eyes, d["eyes"]);
      c.loop = d["loop"].as<bool>();
      break;
    }
    case A_GESTURE: {
      static const char* const K[] = {"name"};
      if (!strictKeys(d, K, 1, has) || has != 0x1 || !isNonEmpty(d["name"])) FAIL("gesture.name");
      copyClip(c.name, sizeof c.name, d["name"]);
      break;
    }
    case A_MOVE: {   // discriminated on cmd; speeds optional only on stop (ruling 4)
      static const char* const K[] = {"cmd", "linear_mms", "angular_mrad_s", "max_dist_mm"};
      if (!strictKeys(d, K, 4, has) || !(has & 1) || !isStr(d["cmd"])) FAIL("move keys");
      if ((e = contractIndex(MOVE_COMMANDS, d["cmd"].as<const char*>())) < 0) FAIL("move.cmd");
      c.moveCmd = e;
      bool needSpeeds = c.moveCmd != MV_STOP;
      if (!(has & 8) || !isNonNegNumber(d["max_dist_mm"], c.maxDistMm)) FAIL("move.max_dist_mm");
      if ((needSpeeds && !(has & 2)) || ((has & 2) && !isNonNegNumber(d["linear_mms"], c.linearMms))) FAIL("move.linear_mms");
      if ((needSpeeds && !(has & 4)) || ((has & 4) && !isNonNegNumber(d["angular_mrad_s"], c.angularMradS))) FAIL("move.angular_mrad_s");
      break;
    }
    case A_PLAY: {
      static const char* const K[] = {"content_id", "source"};
      if (!strictKeys(d, K, 2, has) || has != 0x3) FAIL("play keys");
      if (!isNonEmpty(d["content_id"]) || !copyExact(c.contentId, sizeof c.contentId, d["content_id"])) FAIL("play.content_id");
      if (!isStr(d["source"]) || (e = contractIndex(PLAY_SOURCES, d["source"].as<const char*>())) < 0) FAIL("play.source");
      c.source = e;
      break;
    }
    case A_SET_MODE: {
      static const char* const K[] = {"mode"};
      if (!strictKeys(d, K, 1, has) || has != 0x1 || !isStr(d["mode"]) || (e = contractIndex(MODES, d["mode"].as<const char*>())) < 0) FAIL("set_mode.mode");
      c.mode = e;
      break;
    }
    case A_LED: {
      static const char* const K[] = {"target", "color", "effect", "brightness"};
      if (!strictKeys(d, K, 4, has) || has != 0xF) FAIL("led keys");
      if (!isStr(d["target"]) || (e = contractIndex(LED_TARGETS, d["target"].as<const char*>())) < 0) FAIL("led.target");
      c.ledTarget = e;
      if (!isStr(d["color"]) || !isColor(d["color"].as<const char*>(), c.rgb)) FAIL("led.color");
      if (!isStr(d["effect"]) || (e = contractIndex(LED_EFFECTS, d["effect"].as<const char*>())) < 0) FAIL("led.effect");
      c.ledEffect = e;
      if (!isInt(d["brightness"], 0, 100, iv)) FAIL("led.brightness");
      c.brightness = iv;
      break;
    }
    case A_VOLUME: {
      static const char* const K[] = {"level"};
      if (!strictKeys(d, K, 1, has) || has != 0x1 || !isInt(d["level"], 0, 100, iv)) FAIL("volume.level");
      c.level = iv;
      break;
    }
    case A_CONFIG: {   // a delta: at least one key; active_child_id may be null (present)
      static const char* const K[] = {"volume", "quiet_hours", "movement_on", "language", "active_child_id"};
      if (!strictKeys(d, K, 5, has) || !has) FAIL("config keys");
      if (has & 1) {
        if (!isInt(d["volume"], 0, 100, iv)) FAIL("config.volume");
        c.level = iv; c.cfg |= CFG_VOLUME;
      }
      if (has & 2) {
        static const char* const Q[] = {"start", "end"};
        uint32_t qh;
        JsonVariantConst q = d["quiet_hours"];
        if (!q.is<JsonObjectConst>() || !strictKeys(q.as<JsonObjectConst>(), Q, 2, qh) || qh != 0x3) FAIL("config.quiet_hours");
        if (!isStr(q["start"]) || !isHHMM(q["start"].as<const char*>()) || !isStr(q["end"]) || !isHHMM(q["end"].as<const char*>())) FAIL("config.quiet_hours");
        strcpy(c.quietStart, q["start"].as<const char*>());
        strcpy(c.quietEnd, q["end"].as<const char*>());
        c.cfg |= CFG_QUIET;
      }
      if (has & 4) {
        if (!d["movement_on"].is<bool>()) FAIL("config.movement_on");
        c.movementOn = d["movement_on"].as<bool>(); c.cfg |= CFG_MOVEMENT;
      }
      if (has & 8) {
        if (!isStr(d["language"]) || !isLang(d["language"].as<const char*>())) FAIL("config.language");
        strcpy(c.language, d["language"].as<const char*>()); c.cfg |= CFG_LANGUAGE;
      }
      if (has & 16) {
        JsonVariantConst ch = d["active_child_id"];
        if (ch.isNull()) c.childNull = true;
        else if (!isStr(ch) || !copyExact(c.childId, sizeof c.childId, ch)) FAIL("config.active_child_id");
        c.cfg |= CFG_CHILD;
      }
      break;
    }
    case A_STOP_AUDIO:
      if (d.size() != 0) FAIL("stop_audio.data must be {}");
      break;
    case A_OTA: {
      static const char* const K[] = {"manifest_url"};
      if (!strictKeys(d, K, 1, has) || has != 0x1 || !isStr(d["manifest_url"]) || !isHttpsUrl(d["manifest_url"].as<const char*>()) ||
          !copyExact(c.manifestUrl, sizeof c.manifestUrl, d["manifest_url"]))
        FAIL("ota.manifest_url");
      break;
    }
  }
  return PARSE_OK;
}

// ---------------------------------------------------------------------------
// builders
// ---------------------------------------------------------------------------
static size_t emit(JsonDocument& doc, char* buf, size_t n) {
  if (measureJson(doc) >= n) return 0;
  return serializeJson(doc, buf, n);
}

static void envelope(JsonDocument& doc, uint32_t ts, const char* type) {
  doc["proto_ver"] = PROTO_VER;
  doc["ts"] = ts;
  doc["type"] = type;
}

size_t wireHello(char* buf, size_t n, uint32_t ts, const char* deviceId, const char* fwVer, const char* auth) {
  JsonDocument doc;
  envelope(doc, ts, "hello");
  doc["device_id"] = deviceId;
  doc["fw_ver"] = fwVer;
  doc["auth"] = auth;
  doc["proto"] = PROTO_VER;
  return emit(doc, buf, n);
}

size_t wireStatus(char* buf, size_t n, uint32_t ts, uint8_t batteryPct, uint8_t mode, int rssi,
                  const char* const* faults, uint8_t nFaults, const char* childId, int tempC) {
  JsonDocument doc;
  envelope(doc, ts, "status");
  doc["battery_pct"] = batteryPct;
  doc["charging"] = false;                 // cannot be sensed on this hardware (§2.6, G24)
  doc["mode"] = MODES[mode < 5 ? mode : 0];
  doc["wifi_rssi"] = rssi;
  JsonArray f = doc["fault"].to<JsonArray>();
  for (uint8_t i = 0; i < nFaults; i++) f.add(faults[i]);
  if (childId) doc["active_child_id"] = childId;
  else doc["active_child_id"] = nullptr;
  doc["temp_c"] = tempC;
  return emit(doc, buf, n);
}

size_t wireAck(char* buf, size_t n, uint32_t ts, const char* cmdId, uint8_t status, uint8_t reason) {
  JsonDocument doc;
  envelope(doc, ts, "ack");
  doc["cmd_id"] = cmdId;
  doc["status"] = ACK_STATUSES[status & 3];
  doc["reason"] = reason < 8 ? ACK_REASONS[reason] : "";
  return emit(doc, buf, n);
}

size_t wireAudioMarker(char* buf, size_t n, uint32_t ts, bool start, const char* turnId) {
  JsonDocument doc;
  envelope(doc, ts, start ? "audio_start" : "audio_end");
  doc["turn_id"] = turnId;
  return emit(doc, buf, n);
}

static JsonObject eventHead(JsonDocument& doc, uint32_t ts, const char* id, bool tu, uint8_t kind) {
  envelope(doc, ts, "event");
  doc["event_id"] = id;
  doc["event"] = EVENT_KINDS[kind];
  if (tu) doc["time_uncertain"] = true;
  return doc["data"].to<JsonObject>();
}

static double round2(float v) { return round((double)v * 100.0) / 100.0; }

size_t wireEventTouch(char* buf, size_t n, uint32_t ts, const char* id, bool tu, uint8_t zone, uint8_t gesture) {
  JsonDocument doc;
  JsonObject d = eventHead(doc, ts, id, tu, EV_TOUCH);
  d["zone"] = TOUCH_ZONES[zone];
  d["gesture"] = TOUCH_GESTURES[gesture & 3];
  return emit(doc, buf, n);
}

size_t wireEventWake(char* buf, size_t n, uint32_t ts, const char* id, bool tu, uint8_t source, int dirDeg) {
  JsonDocument doc;
  JsonObject d = eventHead(doc, ts, id, tu, EV_WAKE);
  d["source"] = WAKE_SOURCES[source & 1];
  if (dirDeg >= 0) d["dir_deg"] = dirDeg % 360;
  return emit(doc, buf, n);
}

size_t wireEventCry(char* buf, size_t n, uint32_t ts, const char* id, bool tu, float confidence, float durationS) {
  JsonDocument doc;
  JsonObject d = eventHead(doc, ts, id, tu, EV_CRY);
  d["confidence"] = round2(confidence < 0 ? 0 : confidence > 1 ? 1 : confidence);
  d["duration_s"] = round2(durationS < 0 ? 0 : durationS);
  return emit(doc, buf, n);
}

size_t wireEventMotion(char* buf, size_t n, uint32_t ts, const char* id, bool tu, uint8_t kind, float value) {
  JsonDocument doc;
  JsonObject d = eventHead(doc, ts, id, tu, EV_MOTION);
  d["event"] = MOTION_KINDS[kind < 5 ? kind : 0];
  d["value"] = round2(value);
  return emit(doc, buf, n);
}

size_t wireEventButton(char* buf, size_t n, uint32_t ts, const char* id, bool tu, uint8_t action) {
  JsonDocument doc;
  JsonObject d = eventHead(doc, ts, id, tu, EV_BUTTON);
  d["button"] = BUTTONS[0];
  d["action"] = BUTTON_ACTIONS[action & 1];
  return emit(doc, buf, n);
}

size_t wireEventInterrupt(char* buf, size_t n, uint32_t ts, const char* id, bool tu) {
  JsonDocument doc;
  eventHead(doc, ts, id, tu, EV_INTERRUPT);   // data: {} exactly
  return emit(doc, buf, n);
}

size_t wireEventError(char* buf, size_t n, uint32_t ts, const char* id, bool tu, const char* code, const char* detail) {
  JsonDocument doc;
  JsonObject d = eventHead(doc, ts, id, tu, EV_ERROR);
  d["code"] = code;
  d["detail"] = detail;
  return emit(doc, buf, n);
}

void wireRandomId(char* out, const char* prefix, const uint8_t r[16]) {
  static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  size_t p = strlen(prefix);
  memcpy(out, prefix, p);
  for (int i = 0, o = p; i < 16; i += 3) {   // base64url, no padding: 16 bytes -> 22 chars
    uint32_t v = r[i] << 16 | (i + 1 < 16 ? r[i + 1] << 8 : 0) | (i + 2 < 16 ? r[i + 2] : 0);
    out[o++] = B64[(v >> 18) & 63];
    out[o++] = B64[(v >> 12) & 63];
    if (i + 1 < 16) out[o++] = B64[(v >> 6) & 63];
    if (i + 2 < 16) out[o++] = B64[v & 63];
    out[o] = 0;
  }
}

static uint32_t fnv1aUtf16(const char* s, size_t n) {   // the gateway hashes UTF-16 code units
  uint32_t h = 0x811c9dc5;
  for (size_t i = 0; i < n; i++) {
    uint8_t lo = (uint8_t)s[i];
    h = (h ^ lo) * 0x01000193;
    h = (h ^ 0) * 0x01000193;                    // ASCII: the high byte is 0
  }
  return h;
}

void wirePairingCode(const char* deviceId, const char* fwVer, char out[7]) {
  static const char A[] = "0123456789BCDFGHJKLMNPQRSTVWXYZ";
  char joined[160];
  size_t a = strlen(deviceId), b = strlen(fwVer);
  if (a + b + 1 > sizeof joined) a = b = 0;
  memcpy(joined, deviceId, a);
  joined[a] = 0;                                  // NUL joins the two (never in either)
  memcpy(joined + a + 1, fwVer, b);
  uint32_t h = fnv1aUtf16(joined, a + 1 + b);
  for (int i = 0; i < 6; i++) {
    out[i] = A[h % 31];
    char num[12];
    int len = snprintf(num, sizeof num, "%lu", (unsigned long)h);
    h = fnv1aUtf16(num, len);
  }
  out[6] = 0;
}
