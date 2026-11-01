#include "Net.h"
#include "../State.h"
#include "../store/Store.h"
#include "../body/Safety.h"
#include "../ota/Ota.h"
#include "../../Config.h"
#include <WiFi.h>
#include <sys/time.h>
#include "mqtt_client.h"
#include "esp_sntp.h"
#include "esp_crt_bundle.h"
#include "esp_random.h"

// ===========================================================================
// clock (§4.4): until the first SNTP sync since boot, ts = last saved epoch +
// uptime and events say time_uncertain. RTC memory survives a brownout reset
// (NVS lags by up to 10 min), so it is the better guess when it is valid.
// ===========================================================================
RTC_NOINIT_ATTR static uint32_t rtcEpoch;
RTC_NOINIT_ATTR static uint32_t rtcCheck;
static uint32_t approxBoot = 0;
static char tzBuf[16];
static volatile bool syncedJustNow = false;

uint32_t netApproxBootEpoch() { return approxBoot; }

uint32_t netEpoch(bool* uncertain) {
  if (uncertain) *uncertain = !S.timeSynced;
  return (uint32_t)time(nullptr);
}

static void clockBegin() {
  uint32_t nvsEpoch = storeEpochLoad();
  bool rtcOk = rtcCheck == (rtcEpoch ^ 0x5A17C0DE) && rtcEpoch >= nvsEpoch;
  approxBoot = max<uint32_t>(rtcOk ? rtcEpoch : nvsEpoch, 1700000000);   // a plausible floor before any save
  struct timeval tv = {(time_t)approxBoot, 0};
  settimeofday(&tv, nullptr);
  int off = cal.tzOffsetMin;   // POSIX TZ has the sign inverted: IST is "UTC-05:30"
  snprintf(tzBuf, sizeof tzBuf, "UTC%c%02d:%02d", off >= 0 ? '-' : '+', abs(off) / 60, abs(off) % 60);
  setenv("TZ", tzBuf, 1);
  tzset();
  sntp_set_time_sync_notification_cb([](struct timeval*) {
    if (!S.timeSynced) syncedJustNow = true;
    S.timeSynced = true;
  });
}

// ===========================================================================
// topics, ids, logging
// ===========================================================================
static char tStatus[64], tEvent[64], tCmd[64], tAck[64];
static char pairing[7];
const char* netPairingCode() { return pairing; }

static void makeId(char* out, const char* prefix) {
  uint8_t r[16];
  esp_fill_random(r, sizeof r);
  wireRandomId(out, prefix, r);
}

// ===========================================================================
// outbox + offline queue (§4.5): 500 events in PSRAM, oldest dropped when
// full, ONE in flight, removed only on PUBACK. Cry events also live in NVS.
// ===========================================================================
struct OutEvent { uint16_t len; uint8_t cry; char id[26]; char desc[40]; char json[250]; };
#define OUTQ 500
static OutEvent* ring;
static uint16_t qHead = 0, qCount = 0;
static SemaphoreHandle_t qMx;
static int inflight = -1;               // MQTT msg_id of the event on the wire
static uint32_t inflightAt = 0;

static void enqueueEvent(const char* json, size_t len, const char* id, const char* desc, bool cry) {
  if (!len || len >= sizeof(OutEvent::json)) { Serial.println("[wire] event did not fit: dropped"); return; }
  xSemaphoreTake(qMx, portMAX_DELAY);
  if (qCount == OUTQ) {                 // full: drop the oldest that is NOT on the wire
    uint16_t victim = inflight >= 0 ? (qHead + 1) % OUTQ : qHead;
    Serial.printf("[wire] queue full: dropped %s\n", ring[victim].id);
    if (victim != qHead) ring[victim] = ring[qHead];
    qHead = (qHead + 1) % OUTQ;
    qCount--;
  }
  OutEvent& e = ring[(qHead + qCount) % OUTQ];
  e.len = len; e.cry = cry;
  strlcpy(e.id, id, sizeof e.id);
  strlcpy(e.desc, desc, sizeof e.desc);
  memcpy(e.json, json, len);
  e.json[len] = 0;
  qCount++;
  xSemaphoreGive(qMx);
  if (cry) cryQueueAdd(e.json);
}

#define EVENT_BUF 250
#define STAMP char id[26]; makeId(id, "ev_"); bool tu; uint32_t ts = netEpoch(&tu); char buf[EVENT_BUF]; char desc[40]

void netEventTouch(uint8_t z, uint8_t g) {
  STAMP;
  snprintf(desc, sizeof desc, "touch zone=%s gesture=%s", TOUCH_ZONES[z], TOUCH_GESTURES[g & 3]);
  enqueueEvent(buf, wireEventTouch(buf, sizeof buf, ts, id, tu, z, g), id, desc, false);
}
void netEventWake(uint8_t source, int16_t dirDeg) {
  STAMP;
  snprintf(desc, sizeof desc, "wake %s dir=%d", WAKE_SOURCES[source & 1], dirDeg);
  enqueueEvent(buf, wireEventWake(buf, sizeof buf, ts, id, tu, source, dirDeg), id, desc, false);
}
void netEventCry(float confidence, float durationS) {
  STAMP;
  snprintf(desc, sizeof desc, "cry conf=%.2f dur=%.1f", confidence, durationS);
  enqueueEvent(buf, wireEventCry(buf, sizeof buf, ts, id, tu, confidence, durationS), id, desc, true);
}
void netEventMotion(uint8_t kind, float value) {
  STAMP;
  snprintf(desc, sizeof desc, "motion %s value=%.2f", MOTION_KINDS[kind < 5 ? kind : 0], value);
  enqueueEvent(buf, wireEventMotion(buf, sizeof buf, ts, id, tu, kind, value), id, desc, false);
}
void netEventButton(uint8_t action) {
  STAMP;
  snprintf(desc, sizeof desc, "button belly_talk %s", BUTTON_ACTIONS[action & 1]);
  enqueueEvent(buf, wireEventButton(buf, sizeof buf, ts, id, tu, action), id, desc, false);
}
void netEventInterrupt() {
  STAMP;
  snprintf(desc, sizeof desc, "interrupt");
  enqueueEvent(buf, wireEventInterrupt(buf, sizeof buf, ts, id, tu), id, desc, false);
}
void netEventError(const char* code, const char* detail) {
  STAMP;
  snprintf(desc, sizeof desc, "error %s", code);
  enqueueEvent(buf, wireEventError(buf, sizeof buf, ts, id, tu, code, detail), id, desc, false);
}

// ===========================================================================
// idempotency (§4.6): the last 1000 cmd_ids and the acks each one got
// ===========================================================================
struct IdemRec { uint64_t h; uint8_t n; uint8_t acks[3]; };   // ack = status << 4 | reason (0xF: "")
#define IDEM 1000
static IdemRec* idem;
static uint16_t idemNext = 0, idemCount = 0;
static SemaphoreHandle_t idemMx;

static uint64_t fnv64(const char* s) {
  uint64_t h = 1469598103934665603ULL;
  while (*s) { h ^= (uint8_t)*s++; h *= 1099511628211ULL; }
  return h;
}
static int idemFind(uint64_t h) {
  for (int i = 0; i < idemCount; i++) if (idem[i].h == h) return i;
  return -1;
}
static void idemRecord(const char* cmdId, uint8_t status, uint8_t reason) {
  xSemaphoreTake(idemMx, portMAX_DELAY);
  int i = idemFind(fnv64(cmdId));
  if (i >= 0 && idem[i].n < 3) idem[i].acks[idem[i].n++] = status << 4 | (reason & 0x0F);
  xSemaphoreGive(idemMx);
}

// ===========================================================================
// MQTT
// ===========================================================================
enum Link : uint8_t { L_DOWN, L_CONNECTING, L_SUBSCRIBING, L_HELLO, L_READY };
static esp_mqtt_client_handle_t client = nullptr;
static volatile uint8_t linkState = L_DOWN;
static QueueHandle_t netQ, ackQ, cmdQ;
struct NetEv { int32_t id; int msgId; int code; };
struct AckReq { char cmdId[64]; uint8_t status, reason; };

// Publishing only on a READY link: esp-mqtt would otherwise queue a QoS 1
// message and flush it on reconnect BEFORE the hello (Document C §2.3).
static void publishAck(const char* cmdId, uint8_t status, uint8_t reason) {
  bool sent = false;
  if (linkState == L_READY) {
    char buf[200];
    size_t n = wireAck(buf, sizeof buf, netEpoch(), cmdId, status, reason);
    sent = n && esp_mqtt_client_publish(client, tAck, buf, n, 1, 0) >= 0;
  }
  Serial.printf("[wire] ack %s %s%s%s%s\n", cmdId, ACK_STATUSES[status & 3], reason < 8 ? " / " : "",
                reason < 8 ? ACK_REASONS[reason] : "", sent ? "" : "  (link down: recorded, not sent)");
}

static void ack(const char* cmdId, uint8_t status, uint8_t reason) {
  idemRecord(cmdId, status, reason);
  publishAck(cmdId, status, reason);
}

void netAck(const char* cmdId, uint8_t status, uint8_t reason) {
  idemRecord(cmdId, status, reason);
  AckReq a;
  strlcpy(a.cmdId, cmdId, sizeof a.cmdId);
  a.status = status; a.reason = reason;
  xQueueSend(ackQ, &a, 0);              // the net task publishes: this caller may be the render loop
}

bool netNextCmd(Cmd& c) { return cmdQ && xQueueReceive(cmdQ, &c, 0) == pdTRUE; }

// Command intake (§4.6), in the MQTT task: accepted/rejected go out at once (< 50 ms).
static void intake(const char* data, int len) {
  static Cmd c;
  const char* why = "";
  ParseResult r = wireParseCmd(data, len, c, &why);
  if (r == PARSE_PROTO) {               // keep last-known-good, report, do not act
    faultFor(F_PROTO_MISMATCH, 60000);
    Serial.println("[wire] cmd ignored (no ack): proto_ver mismatch");
    return;
  }
  if (r == PARSE_NOT_JSON || r == PARSE_INVALID) {
    faultFor(F_CMD_INVALID, 60000);
    Serial.printf("[wire] cmd ignored (no ack): %s\n", why);
    return;
  }
  uint64_t h = fnv64(c.cmdId);
  uint8_t prev[3], nPrev = 0;
  bool dup = false;
  xSemaphoreTake(idemMx, portMAX_DELAY);
  int i = idemFind(h);
  if (i >= 0) { dup = true; nPrev = idem[i].n; memcpy(prev, idem[i].acks, 3); }
  else {
    idem[idemNext] = {h, 0, {0, 0, 0}};
    idemNext = (idemNext + 1) % IDEM;
    if (idemCount < IDEM) idemCount++;
  }
  xSemaphoreGive(idemMx);
  if (dup) {                            // re-publish the acks already sent, in order; apply nothing
    Serial.printf("[wire] cmd %s is a duplicate: re-sending %u ack(s)\n", c.cmdId, nPrev);
    for (uint8_t k = 0; k < nPrev; k++) publishAck(c.cmdId, prev[k] >> 4, (prev[k] & 0x0F) == 0x0F ? R_NONE : prev[k] & 0x0F);
    return;
  }
  if (r == PARSE_UNKNOWN_ACTION) { ack(c.cmdId, ACK_REJECTED, R_UNKNOWN_ACTION); return; }

  uint8_t reason = R_NONE;               // the hardware's truth, final (Document C §2.5)
  switch (c.action) {
    case A_GESTURE: reason = refuseGesture(); break;
    case A_MOVE:    reason = refuseMove(c.moveCmd == MV_STOP); break;
    case A_PLAY:    reason = refusePlay(); break;
    case A_SAY:     if (!S.turnOpen) reason = refuseSay(); break;
    case A_OTA:     reason = refuseOta(); break;
    default: break;
  }
  if (reason != R_NONE) { ack(c.cmdId, ACK_REJECTED, reason); return; }
  ack(c.cmdId, ACK_ACCEPTED, R_NONE);
  if (xQueueSend(cmdQ, &c, 0) != pdTRUE) ack(c.cmdId, ACK_FAILED, R_NONE);
}

static void onMqtt(void*, esp_event_base_t, int32_t id, void* data) {
  auto* e = (esp_mqtt_event_handle_t)data;
  if (id == MQTT_EVENT_DATA) {
    if (e->current_data_offset == 0 && (e->topic_len != (int)strlen(tCmd) || strncmp(e->topic, tCmd, e->topic_len))) return;
    if (e->current_data_offset != 0 || e->data_len != e->total_data_len) {
      faultFor(F_CMD_INVALID, 60000);
      Serial.println("[wire] cmd larger than the MQTT buffer: ignored");
      return;
    }
    intake(e->data, e->data_len);
    return;
  }
  NetEv ev = {id, e->msg_id, 0};
  if (id == MQTT_EVENT_ERROR && e->error_handle && e->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
    ev.code = e->error_handle->connect_return_code;
  xQueueSend(netQ, &ev, 0);
}

// ===========================================================================
// net task: connection sequence (§4.2), heartbeat, outbox pump
// ===========================================================================
static int subId = -1, helloId = -1;
static uint8_t fails = 0;
static bool authRefused = false, started = false;
static uint32_t nextAttempt = 0, lastStatus = 0;

static void scheduleRetry(uint32_t now) {
  // 1 -> 2 -> 4 -> 8 -> 16 -> 30 s, +-20 % jitter; a refused token waits 60 s (no tight loop, Doc C §2.1)
  uint32_t base = authRefused ? 60000 : min<uint32_t>(30000, 1000u << min<uint8_t>(fails, 5));
  int32_t jitter = (int32_t)(base / 5) * ((int32_t)(esp_random() % 2001) - 1000) / 1000;
  nextAttempt = now + base + jitter;
  if (fails < 255) fails++;
}

static void sendStatus() {
  const char* names[F_COUNT];
  uint8_t n = 0;
  uint32_t m = faultMask();
  for (uint8_t f = 0; f < F_COUNT; f++) if (m & (1u << f)) names[n++] = FAULT_NAMES[f];
  static char buf[1024];
  size_t len = wireStatus(buf, sizeof buf, netEpoch(), S.batteryPct, S.mode, S.rssi, names, n,
                          settings.childSet ? settings.childId : nullptr, (int)lroundf(temperatureRead()));
  if (len) esp_mqtt_client_publish(client, tStatus, buf, len, 0, 0);
  lastStatus = millis();
}

static void onNetEvent(const NetEv& ev, uint32_t now) {
  switch (ev.id) {
    case MQTT_EVENT_CONNECTED:          // 2. subscribe to cmd at QoS 1, nothing else
      linkState = L_SUBSCRIBING;
      subId = esp_mqtt_client_subscribe(client, tCmd, 1);
      configTzTime(tzBuf, "pool.ntp.org", "time.google.com");   // SNTP on every connect
      break;
    case MQTT_EVENT_SUBSCRIBED:         // 3. on SUBACK, hello on status at QoS 1
      if (ev.msgId != subId) break;
      fails = 0; authRefused = false;
      {
        char buf[320];
        size_t n = wireHello(buf, sizeof buf, netEpoch(), settings.deviceId, FW_VER, settings.token);
        helloId = esp_mqtt_client_publish(client, tStatus, buf, n, 1, 0);
      }
      linkState = L_HELLO;
      break;
    case MQTT_EVENT_PUBLISHED:
      if (ev.msgId == helloId && linkState == L_HELLO) {   // 4. status now and every 15 s; 5. replay
        linkState = L_READY;
        S.mqttUp = true;
        sendStatus();
        Serial.printf("[wire] hello acked, status sent, replaying %u queued event(s)\n", qCount);
        otaMarkValid();                 // a new image is good once hello + status work (§13.4)
      } else if (ev.msgId == inflight) {
        xSemaphoreTake(qMx, portMAX_DELAY);
        OutEvent& e = ring[qHead];
        Serial.printf("[wire] ev %s id=%s puback %lu ms\n", e.desc, e.id, (unsigned long)(now - inflightAt));
        if (e.cry) cryQueueRemove(e.id);
        qHead = (qHead + 1) % OUTQ;
        qCount--;
        inflight = -1;
        xSemaphoreGive(qMx);
      }
      break;
    case MQTT_EVENT_DISCONNECTED:
      if (linkState >= L_SUBSCRIBING) Serial.println("[wire] broker disconnected");
      linkState = L_DOWN;
      S.mqttUp = false;
      inflight = -1;                    // the head stays queued and goes again after the next hello
      scheduleRetry(now);
      break;
    case MQTT_EVENT_ERROR:
      if (ev.code == MQTT_CONNECTION_REFUSE_BAD_USERNAME || ev.code == MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED) {
        authRefused = true;
        Serial.println("[wire] broker refused this device's token: next try in 60 s (fix the token)");
      }
      break;
  }
}

static void netTask(void*) {
  for (;;) {
    NetEv ev;
    if (xQueueReceive(netQ, &ev, pdMS_TO_TICKS(50)) == pdTRUE) onNetEvent(ev, millis());
    uint32_t now = millis();

    bool wifi = WiFi.isConnected();
    if (wifi != S.wifiUp) {
      S.wifiUp = wifi;
      if (wifi) {
        Serial.printf("[wifi] up %s rssi %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
        nextAttempt = now;
      } else Serial.println("[wifi] down");
    }
    if (wifi) S.rssi = WiFi.RSSI();

    if (client && wifi && linkState == L_DOWN && (int32_t)(now - nextAttempt) >= 0) {
      linkState = L_CONNECTING;
      if (!started) { esp_mqtt_client_start(client); started = true; }
      else esp_mqtt_client_reconnect(client);
      nextAttempt = now + 20000;        // guard: an attempt that never reports back
    }
    if (linkState == L_CONNECTING && (int32_t)(now - nextAttempt) >= 0) { linkState = L_DOWN; scheduleRetry(now); }

    if (linkState == L_READY) {
      if (now - lastStatus >= 15000) sendStatus();
      AckReq a;
      while (xQueueReceive(ackQ, &a, 0) == pdTRUE) publishAck(a.cmdId, a.status, a.reason);
      if (inflight >= 0 && now - inflightAt > 10000) inflight = -1;   // lost PUBACK: send again (ids dedupe)
      if (inflight < 0 && qCount) {
        static char buf[EVENT_BUF];
        uint16_t len;
        xSemaphoreTake(qMx, portMAX_DELAY);
        len = ring[qHead].len;
        memcpy(buf, ring[qHead].json, len);
        xSemaphoreGive(qMx);
        int id = esp_mqtt_client_publish(client, tEvent, buf, len, 1, 0);
        if (id >= 0) { inflight = id; inflightAt = now; }
      }
    } else {
      AckReq a;                         // terminal acks while the link is down: recorded, not sent
      while (xQueueReceive(ackQ, &a, 0) == pdTRUE) publishAck(a.cmdId, a.status, a.reason);
    }
  }
}

// ===========================================================================
void netHousekeeping(uint32_t now) {
  static uint32_t rtcAt = 0, saveAt = 0;
  if (now - rtcAt >= 1000) {
    rtcAt = now;
    rtcEpoch = netEpoch();
    rtcCheck = rtcEpoch ^ 0x5A17C0DE;
  }
  if (syncedJustNow || now - saveAt >= 600000) {   // every 10 min, and right after a sync
    syncedJustNow = false;
    saveAt = now;
    storeEpochSave(netEpoch());
  }
  int f = faultTakeNewError();          // a NEW hardware fault: one error event per boot
  if (f >= 0) {
    static const char* const DETAIL[] = {"not answering", "supply above limit, torque kept off",
                                         "overloaded, torque off", "too hot, torque off"};
    const char* detail = "hardware fault";
    if (f >= F_SERVO_MISSING && f <= F_SERVO_OVERHEAT + 2) detail = DETAIL[(f - F_SERVO_MISSING) / 3];
    else if (f <= F_CLIFF_REAR_MISSING) detail = "not answering";
    else if (f == F_I2C_BUS_STUCK) detail = "I2C data line held low";
    else if (f == F_MIC_SILENT) detail = "mic data line static";
    else if (f == F_ENCODER_L_DEAD || f == F_ENCODER_R_DEAD) detail = "no encoder pulses while driven";
    netEventError(FAULT_NAMES[f], detail);
  }
}

void netStatus(Print& out) {
  static const char* const LN[] = {"down", "connecting", "subscribing", "hello sent", "ready"};
  bool tu;
  uint32_t ts = netEpoch(&tu);
  out.printf("wifi %s (%s) rssi %d | mqtt %s (%s) | clock %lu%s | queue %u in flight %d\n",
             S.wifiUp ? "up" : "down", settings.wifiSsid[0] ? settings.wifiSsid : "no ssid", S.rssi,
             LN[linkState], settings.mqttUrl[0] ? settings.mqttUrl : "no url", (unsigned long)ts,
             tu ? " (uncertain)" : "", qCount, inflight);
}

void netBegin() {
  clockBegin();
  wirePairingCode(settings.deviceId, FW_VER, pairing);
  snprintf(tStatus, sizeof tStatus, TOPIC_PREFIX "/%s/status", settings.deviceId);
  snprintf(tEvent, sizeof tEvent, TOPIC_PREFIX "/%s/event", settings.deviceId);
  snprintf(tCmd, sizeof tCmd, TOPIC_PREFIX "/%s/cmd", settings.deviceId);
  snprintf(tAck, sizeof tAck, TOPIC_PREFIX "/%s/ack", settings.deviceId);
  ring = (OutEvent*)ps_malloc(OUTQ * sizeof(OutEvent));
  idem = (IdemRec*)ps_malloc(IDEM * sizeof(IdemRec));
  qMx = xSemaphoreCreateMutex();
  idemMx = xSemaphoreCreateMutex();
  netQ = xQueueCreate(16, sizeof(NetEv));
  ackQ = xQueueCreate(16, sizeof(AckReq));
  cmdQ = xQueueCreate(6, sizeof(Cmd));

  // cry alerts from before the reboot go first, with their original id, ts and data
  static char saved[CRYQ_MAX][CRYQ_SLOT];
  int n = cryQueueLoad(saved, CRYQ_MAX);
  for (int i = 0; i < n; i++) {
    const char* p = strstr(saved[i], "\"event_id\":\"");
    char id[26] = "";
    if (p) { p += 12; size_t k = strcspn(p, "\""); if (k < sizeof id) { memcpy(id, p, k); id[k] = 0; } }
    xSemaphoreTake(qMx, portMAX_DELAY);
    OutEvent& e = ring[(qHead + qCount++) % OUTQ];
    e.len = strlen(saved[i]); e.cry = 1;
    strlcpy(e.id, id, sizeof e.id); strlcpy(e.desc, "cry (saved)", sizeof e.desc);
    memcpy(e.json, saved[i], e.len + 1);
    xSemaphoreGive(qMx);
  }

  // Document C §2.2: print what the console's pairing code is derived from
  Serial.printf("[id] device_id=%s fw_ver=%s pairing=%s token=%s proto_ver=%d FW-SOT=%s%s\n", settings.deviceId,
                FW_VER, pairing, settings.token[0] ? "set" : "not set", PROTO_VER, FW_SOT_VER,
                n ? " (unsent cry alerts restored)" : "");

  if (settings.wifiSsid[0]) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);               // modem sleep would eat the voice latency budget
    WiFi.setAutoReconnect(true);
    WiFi.begin(settings.wifiSsid, settings.wifiPass);
  } else Serial.println("[wifi] no credentials: `wifi SSID PASS` on the console");

  if (settings.mqttUrl[0]) {
    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = settings.mqttUrl;
    if (!strncmp(settings.mqttUrl, "mqtts", 5)) cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.credentials.client_id = settings.deviceId;      // client id = device_id (§4.2)
    cfg.credentials.username = settings.deviceId;
    cfg.credentials.authentication.password = settings.token;
    cfg.session.keepalive = 15;
    cfg.network.disable_auto_reconnect = true;           // our own backoff, with jitter
    cfg.network.timeout_ms = 5000;
    cfg.buffer.size = 2048;
    cfg.buffer.out_size = 2048;
    cfg.task.stack_size = 6144;
    client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, onMqtt, nullptr);
  } else Serial.println("[wire] no broker URL: `url mqtt://<LAN-IP>:1883 ws://<LAN-IP>:8081` on the console");

  xTaskCreatePinnedToCore(netTask, "net", 6144, nullptr, 12, nullptr, 0);
}
