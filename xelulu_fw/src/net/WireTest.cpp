// `selftest wire`: the contract's own fixtures (packages/shared/src/gateway-contract/
// fixtures @ 79a92d0, copied verbatim) through this firmware's parser, the cases
// that must NOT be acked, and every builder's key set against the fixtures (§14.1).
#include "Net.h"
#include <ArduinoJson.h>

struct Case { const char* json; ParseResult want; uint8_t action; };
static const Case CASES[] = {
  // fixtures/cmd-*.json: every one must parse
  {R"({"proto_ver":2,"cmd_id":"9f2c","action":"say","data":{"expression":"happy","lang":"en-IN","voice":"xelulu","duration_hint_ms":2200}})", PARSE_OK, A_SAY},
  {R"({"proto_ver":2,"cmd_id":"c_animate_01","action":"animate","data":{"expression":"curious","eyes":"blink","loop":false}})", PARSE_OK, A_ANIMATE},
  {R"({"proto_ver":2,"cmd_id":"c_gesture_01","action":"gesture","data":{"name":"wave"}})", PARSE_OK, A_GESTURE},
  {R"({"proto_ver":2,"cmd_id":"c_move_01","action":"move","data":{"cmd":"come","linear_mms":150,"angular_mrad_s":0,"max_dist_mm":2000}})", PARSE_OK, A_MOVE},
  {R"({"proto_ver":2,"cmd_id":"c_play_01","action":"play","data":{"content_id":"story_042","source":"cache"}})", PARSE_OK, A_PLAY},
  {R"({"proto_ver":2,"cmd_id":"c_set_mode_01","action":"set_mode","data":{"mode":"story"}})", PARSE_OK, A_SET_MODE},
  {R"({"proto_ver":2,"cmd_id":"c_led_01","action":"led","data":{"target":"ears","color":"#FFB347","effect":"solid","brightness":60}})", PARSE_OK, A_LED},
  {R"({"proto_ver":2,"cmd_id":"c_volume_01","action":"volume","data":{"level":40}})", PARSE_OK, A_VOLUME},
  {R"({"proto_ver":2,"cmd_id":"c_config_01","action":"config","data":{"volume":40,"quiet_hours":{"start":"20:30","end":"07:00"},"movement_on":true,"language":"hi-IN","active_child_id":"c_01"}})", PARSE_OK, A_CONFIG},
  {R"({"proto_ver":2,"cmd_id":"c_stop_audio_01","action":"stop_audio","data":{}})", PARSE_OK, A_STOP_AUDIO},
  {R"({"proto_ver":2,"cmd_id":"a1b2","action":"ota","data":{"manifest_url":"https://localhost/ota/manifest.json"}})", PARSE_OK, A_OTA},
  // edge cases the schemas accept
  {R"({"proto_ver":2,"cmd_id":"x","action":"config","data":{"active_child_id":null}})", PARSE_OK, A_CONFIG},
  {R"({"proto_ver":2,"cmd_id":"x","action":"move","data":{"cmd":"stop","max_dist_mm":0}})", PARSE_OK, A_MOVE},
  {R"({"proto_ver":2,"cmd_id":"x","action":"say","data":{"expression":"anything","lang":"hi-Deva-IN","voice":"grandma","duration_hint_ms":0}})", PARSE_OK, A_SAY},
  {R"({"proto_ver":2,"cmd_id":"x","action":"dance","data":{}})", PARSE_UNKNOWN_ACTION, A_UNKNOWN},
  // must NOT be acked
  {R"({"proto_ver":1,"cmd_id":"x","action":"volume","data":{"level":4}})", PARSE_PROTO, 0},
  {R"({"cmd_id":"x","action":"volume","data":{"level":4}})", PARSE_PROTO, 0},
  {R"({"proto_ver":2,"cmd_id":"","action":"volume","data":{"level":4}})", PARSE_INVALID, 0},
  {R"({"proto_ver":2,"cmd_id":"x","action":"volume","data":{"level":4},"ts":1})", PARSE_INVALID, 0},
  {R"({"proto_ver":2,"cmd_id":"x","action":"volume","data":{"level":101}})", PARSE_INVALID, 0},
  {R"({"proto_ver":2,"cmd_id":"x","action":"volume","data":{"level":40.5}})", PARSE_INVALID, 0},
  {R"({"proto_ver":2,"cmd_id":"x","action":"volume","data":{"level":"40"}})", PARSE_INVALID, 0},
  {R"({"proto_ver":2,"cmd_id":"x","action":"volume","data":{"level":40,"x":1}})", PARSE_INVALID, 0},
  {R"({"proto_ver":2,"cmd_id":"x","action":"say","data":{"expression":"happy","lang":"EN","voice":"xelulu","duration_hint_ms":1}})", PARSE_INVALID, 0},
  {R"({"proto_ver":2,"cmd_id":"x","action":"led","data":{"target":"ears","color":"#12345","effect":"solid","brightness":5}})", PARSE_INVALID, 0},
  {R"({"proto_ver":2,"cmd_id":"x","action":"led","data":{"target":"Ears","color":"#123456","effect":"solid","brightness":5}})", PARSE_INVALID, 0},
  {R"({"proto_ver":2,"cmd_id":"x","action":"config","data":{}})", PARSE_INVALID, 0},
  {R"({"proto_ver":2,"cmd_id":"x","action":"config","data":{"quiet_hours":{"start":"24:00","end":"07:00"}}})", PARSE_INVALID, 0},
  {R"({"proto_ver":2,"cmd_id":"x","action":"move","data":{"cmd":"come","linear_mms":100,"max_dist_mm":500}})", PARSE_INVALID, 0},
  {R"({"proto_ver":2,"cmd_id":"x","action":"move","data":{"cmd":"turn","linear_mms":0,"angular_mrad_s":-1,"max_dist_mm":5}})", PARSE_INVALID, 0},
  {R"({"proto_ver":2,"cmd_id":"x","action":"stop_audio","data":{"now":true}})", PARSE_INVALID, 0},
  {R"({"proto_ver":2,"cmd_id":"x","action":"ota","data":{"manifest_url":"http://x/m.json"}})", PARSE_INVALID, 0},
  {"not json", PARSE_NOT_JSON, 0},
  {"[1,2]", PARSE_NOT_JSON, 0},
};

// exact key set (and order-free) of a built message
static bool keys(const char* json, const char* expect, const char* dataExpect = nullptr) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  auto match = [](JsonObjectConst o, const char* list) {
    size_t n = 0;
    for (JsonPairConst kv : o) {
      char k[40];
      snprintf(k, sizeof k, ",%s,", kv.key().c_str());
      char l[200];
      snprintf(l, sizeof l, ",%s,", list);
      if (!strstr(l, k)) return false;
      n++;
    }
    size_t want = *list ? 1 : 0;
    for (const char* p = list; *p; p++) want += *p == ',';
    return n == want;
  };
  JsonObjectConst o = doc.as<JsonObjectConst>();
  if (!match(o, expect)) return false;
  if (o["proto_ver"].as<int>() != 2 || !o["ts"].is<long long>()) return false;   // ints stay ints
  return !dataExpect || match(o["data"].as<JsonObjectConst>(), dataExpect);
}

bool netSelfTest(Print& out) {
  int fails = 0, n = 0;
  auto check = [&](bool ok, const char* what) {
    n++;
    if (!ok) { fails++; out.printf("  FAIL %s\n", what); }
  };
  static Cmd c;
  for (const Case& k : CASES) {
    ParseResult r = wireParseCmd(k.json, strlen(k.json), c);
    char what[96];
    snprintf(what, sizeof what, "parse %.70s (got %d want %d)", k.json, r, k.want);
    check(r == k.want && (k.want != PARSE_OK || c.action == k.action), what);
  }
  wireParseCmd(CASES[8].json, strlen(CASES[8].json), c);   // config fixture: every field landed
  check(c.cfg == 0x1F && c.level == 40 && !strcmp(c.quietStart, "20:30") && c.movementOn &&
        !strcmp(c.language, "hi-IN") && !strcmp(c.childId, "c_01"), "config fixture fields");

  char b[600], id[26] = "ev_7f3a";
  const char* none[] = {nullptr};
  wireHello(b, sizeof b, 1733400000, "xlu_000123", "0.1.0", "tok");
  check(keys(b, "proto_ver,ts,type,device_id,fw_ver,auth,proto"), "hello keys");
  wireStatus(b, sizeof b, 1733400000, 72, 0, -58, none, 0, "c_01", 31);
  check(keys(b, "proto_ver,ts,type,battery_pct,charging,mode,wifi_rssi,fault,active_child_id,temp_c"), "status keys");
  wireStatus(b, sizeof b, 1733400000, 72, 0, -58, none, 0, nullptr, 31);
  check(strstr(b, "\"active_child_id\":null") != nullptr, "status null child");
  wireAck(b, sizeof b, 1733400005, "9f2c", ACK_REJECTED, R_PICKED_UP);
  check(keys(b, "proto_ver,ts,type,cmd_id,status,reason") && strstr(b, "\"reason\":\"picked_up\""), "ack keys + reason");
  wireAck(b, sizeof b, 1733400005, "9f2c", ACK_DONE, R_NONE);
  check(strstr(b, "\"reason\":\"\"") != nullptr, "ack empty reason");
  wireAudioMarker(b, sizeof b, 1733400001, true, "t_9f");
  check(keys(b, "proto_ver,ts,type,turn_id") && strstr(b, "audio_start"), "audio_start keys");
  wireEventTouch(b, sizeof b, 1733400001, id, false, WZ_BELLY, WG_DOUBLE_TAP);
  check(keys(b, "proto_ver,ts,type,event_id,event,data", "zone,gesture"), "touch keys");
  wireEventTouch(b, sizeof b, 1733400001, id, true, WZ_BELLY, WG_DOUBLE_TAP);
  check(keys(b, "proto_ver,ts,type,event_id,event,time_uncertain,data", "zone,gesture"), "touch time_uncertain");
  wireEventWake(b, sizeof b, 1733400010, id, false, WAKE_HEY_LULU, 45);
  check(keys(b, "proto_ver,ts,type,event_id,event,data", "source,dir_deg"), "wake keys");
  wireEventWake(b, sizeof b, 1733400010, id, false, WAKE_TOUCH, -1);
  check(keys(b, "proto_ver,ts,type,event_id,event,data", "source"), "wake without dir");
  wireEventCry(b, sizeof b, 1733400011, id, false, 0.87f, 6.5f);
  check(keys(b, "proto_ver,ts,type,event_id,event,data", "confidence,duration_s") && strstr(b, "0.87"), "cry keys");
  wireEventMotion(b, sizeof b, 1733400012, id, false, WM_PICKUP, 1);
  check(keys(b, "proto_ver,ts,type,event_id,event,data", "event,value"), "motion keys");
  wireEventButton(b, sizeof b, 1733400013, id, false, BTN_PRESS);
  check(keys(b, "proto_ver,ts,type,event_id,event,data", "button,action"), "button keys");
  wireEventInterrupt(b, sizeof b, 1733400015, id, false);
  check(keys(b, "proto_ver,ts,type,event_id,event,data", "") && strstr(b, "\"data\":{}"), "interrupt data {}");
  wireEventError(b, sizeof b, 1733400016, id, false, "imu_missing", "not answering");
  check(keys(b, "proto_ver,ts,type,event_id,event,data", "code,detail"), "error keys");

  char code[7];
  wirePairingCode("xlu_000123", "0.1.0", code);
  check(!strcmp(code, "5BK8P7"), "pairing code (gateway's algorithm)");
  uint8_t r[16];
  for (int i = 0; i < 16; i++) r[i] = i * 17;
  wireRandomId(id, "ev_", r);
  check(strlen(id) == 25 && strspn(id + 3, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") == 22, "event_id shape");

  out.printf("selftest wire: %d/%d passed%s\n", n - fails, n, fails ? "" : " - contract OK");
  return fails == 0;
}
