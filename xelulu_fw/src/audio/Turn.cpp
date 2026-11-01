#include "Turn.h"
#include "Cry.h"
#include "Sfx.h"
#include "../State.h"
#include "../store/Store.h"
#include "../net/Net.h"
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <opus.h>
#include <atomic>
#include "esp_random.h"

#define FRAME     320          // 20 ms at 16 kHz: one Opus packet per WS binary message (§5.1)
#define MIC_RING  32768        // 2 s: pre-roll and the barge-in buffer (§5.5)
#define PCM_RING  16384        // 1 s of decoded reply
#define MS(x)     ((x) * 16)   // ms -> samples

const char* const TURN_STATES[] = {"idle", "connecting", "uploading", "waiting", "playing", "draining", "followup", "bargein"};

// SPSC rings. mic: audio_io writes, turn reads. pcm: turn writes, audio_io reads.
static int16_t *mic, *pcm;
static std::atomic<uint32_t> micW{0}, pcmW{0}, pcmR{0};
static std::atomic<bool> primed{false};
static std::atomic<uint16_t> fadeReq{0};   // stop: ramp to zero over this many samples, then flush
static uint32_t micR = 0;

static uint32_t buffered() { return pcmW.load(std::memory_order_acquire) - pcmR.load(std::memory_order_acquire); }

// ===========================================================================
// audio_io side
// ===========================================================================
void turnMicIn(const int16_t* p, int n) {
  if (!mic) return;
  uint32_t w = micW.load(std::memory_order_relaxed);
  for (int i = 0; i < n; i++) mic[(w + i) & (MIC_RING - 1)] = p[i];
  micW.store(w + n, std::memory_order_release);
}

bool turnStreamOut(int16_t* out, int n) {
  static uint16_t fadeLen = 0, fadePos = 0;
  if (!pcm || (!primed && !fadeReq)) { memset(out, 0, n * 2); return false; }
  uint32_t r = pcmR.load(std::memory_order_relaxed), w = pcmW.load(std::memory_order_acquire);
  if (fadeReq && !fadeLen) { fadeLen = fadeReq; fadePos = 0; }
  int k = 0;
  for (; k < n && r != w; k++, r++) {
    int32_t s = pcm[r & (PCM_RING - 1)];
    if (fadeLen) s = s * (fadeLen - fadePos) / fadeLen;   // Eillik's cutFade, never a click (§5.4)
    out[k] = s;
    if (fadeLen && ++fadePos >= fadeLen) { k++; r = w; break; }
  }
  for (; k < n; k++) out[k] = 0;                   // underrun: silence, the ring keeps the order
  if (fadeLen && (fadePos >= fadeLen || r == w)) { // done fading (or nothing left): drop the rest
    r = w; fadeLen = 0; primed = false; fadeReq = 0;
  }
  pcmR.store(r, std::memory_order_release);
  return true;
}

// ===========================================================================
// turn task
// ===========================================================================
enum Req : uint8_t { RQ_START, RQ_START_PTT, RQ_RELEASE, RQ_BARGE, RQ_STOP, RQ_GRANDMA, RQ_TEST };
static QueueHandle_t reqQ, evQ;
static WebSocketsClient ws;
static OpusEncoder* enc;
static OpusDecoder* dec;
static volatile TurnState st = T_IDLE;

static char turnId[26], authHdr[120];
static bool wsOpen, wsUp, wsClosed, startSent, remoteEnd, ending, ptt, held, speechSeen, grandma, stopSeen;
static uint32_t stateAt, startAt, lastPktAt, firstPktAt, endMark, bargeMark;
static uint16_t speechRun, silentRun, upFrames, onsetRun;
static uint8_t plcLeft;
static float noiseDb = -60;
static bool toneTest;                              // `turn test`: upload a 1 kHz tone instead of the mic
static uint32_t tConn, tEnd, tAudible;             // latency printout (§5.8, bring-up stage 14)
static uint16_t tailMs;                            // silence the VAD waited out before tEnd

static void post(uint8_t kind, uint8_t a = 0) { TurnEvent e = {kind, a}; xQueueSend(evQ, &e, 0); }

static void setState(TurnState s) {
  st = s;
  stateAt = millis();
  S.turnOpen = s != T_IDLE && s != T_FOLLOWUP;
  Serial.printf("[turn] %s %s\n", TURN_STATES[s], turnId);
}

static void fade(uint16_t samples) { fadeReq = samples; }   // the audio task runs even with the speaker off

// ws[s]://host[:port][/path]
static bool parseUrl(const char* u, bool& tls, char* host, size_t hn, uint16_t& port, const char*& path) {
  if (!strncmp(u, "wss://", 6)) { tls = true; u += 6; }
  else if (!strncmp(u, "ws://", 5)) { tls = false; u += 5; }
  else return false;
  const char* slash = strchr(u, '/');
  const char* end = slash ? slash : u + strlen(u);
  const char* colon = (const char*)memchr(u, ':', end - u);
  size_t len = (colon ? colon : end) - u;
  if (!len || len >= hn) return false;
  memcpy(host, u, len);
  host[len] = 0;
  port = colon ? atoi(colon + 1) : (tls ? 443 : 80);
  path = slash ? slash : "/";
  return port != 0;
}

static void closeWs() {
  if (wsOpen) ws.disconnect();
  wsOpen = wsUp = false;
}

static void sendMarker(bool start) {
  char buf[160];
  uint32_t ts = netEpoch();
  size_t n = wireAudioMarker(buf, sizeof buf, ts, start, turnId);
  if (n) ws.sendTXT((uint8_t*)buf, n);
  if (start) startSent = true;
}

static bool isAudioEnd(const uint8_t* p, size_t len) {
  JsonDocument d;
  if (deserializeJson(d, p, len)) return false;
  return d["type"] == "audio_end" && d["turn_id"] == (const char*)turnId;
}

static void onPacket(const uint8_t* p, size_t len, uint32_t now) {
  if (st == T_WAITING) { firstPktAt = now; setState(T_PLAYING); post(TE_SPEAK); }
  if ((st != T_PLAYING && st != T_DRAINING) || fadeReq) return;   // late frames after a stop or barge-in
  // ponytail: over the 400 ms cap the NEWEST packet is dropped; the gateway paces in real time so this is rare
  if (buffered() > MS(cal.jitterMaxMs)) return;
  static int16_t tmp[1920];                        // 120 ms, Opus's largest frame
  int got = opus_decode(dec, p, len, tmp, 1920, 0);
  if (got <= 0) return;
  uint32_t w = pcmW.load(std::memory_order_relaxed);
  for (int i = 0; i < got; i++) pcm[(w + i) & (PCM_RING - 1)] = tmp[i];
  pcmW.store(w + got, std::memory_order_release);
  lastPktAt = now;
  plcLeft = 3;
}

static void onWs(WStype_t type, uint8_t* p, size_t len) {
  switch (type) {
    case WStype_CONNECTED:    wsUp = true; wsClosed = false; tConn = millis(); sendMarker(true); break;   // gateway start_timeout is 5 s (§5.3)
    case WStype_DISCONNECTED: wsUp = false; wsClosed = true; break;
    case WStype_TEXT:         if (isAudioEnd(p, len)) remoteEnd = true; break;
    case WStype_BIN:          onPacket(p, len, millis()); break;
    default: break;
  }
}

// the core's built-in CA bundle, the same one MQTT verifies against
extern const uint8_t crtStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t crtEnd[] asm("_binary_x509_crt_bundle_end");

static bool wsConnect() {
  bool tls;
  char host[96];
  uint16_t port;
  const char* path;
  if (!parseUrl(settings.audioUrl, tls, host, sizeof host, port, path)) return false;
  snprintf(authHdr, sizeof authHdr, "Authorization: Bearer %s", settings.token);
  ws.setExtraHeaders(authHdr);                     // replaces the library's "Origin: file://"
  if (tls) {
    ws.beginSslWithBundle(host, port, path, crtStart, crtEnd - crtStart, "");   // "": no subprotocol
  } else {
    ws.begin(host, port, path, "");
  }
  ws.onEvent(onWs);
  ws.setReconnectInterval(1000);
  wsOpen = true;
  return true;
}

// a new turn, uploading from mic sample `from` (pre-roll, or the barge-in point)
static void openTurn(uint32_t from) {
  closeWs();
  uint8_t r[16];
  esp_fill_random(r, sizeof r);
  wireRandomId(turnId, "t_", r);
  wsClosed = startSent = remoteEnd = ending = speechSeen = stopSeen = grandma = toneTest = false;
  speechRun = silentRun = upFrames = tailMs = 0;
  tConn = tEnd = tAudible = 0;
  uint32_t w = micW;
  micR = w - from > MIC_RING - FRAME ? w - (MIC_RING - FRAME) : from;
  S.uploading = true;                              // the privacy indicator goes on first (§5.9)
  post(TE_LISTEN);
  cryPause();                                      // paused during turns (§3.5)
  startAt = millis();
  if (!wsConnect()) { S.uploading = false; setState(T_IDLE); post(TE_IDLE, TI_ERROR); return; }
  setState(T_CONNECTING);
}

static void endTurn(TurnEnd why) {
  if (st == T_CONNECTING || st == T_UPLOADING) {
    if (startSent && wsUp) sendMarker(false);      // §5.3: audio_end only if audio_start went out
  }
  fade(MS(8));
  closeWs();
  S.uploading = false;
  setState(T_IDLE);
  post(TE_IDLE, why);
}

static float frameDb(const int16_t* f) {
  float e = 0;
  for (int i = 0; i < FRAME; i++) e += (float)f[i] * f[i];
  return 10 * log10f(e / FRAME / (32768.0f * 32768.0f) + 1e-12f);
}

// ponytail: energy VAD over an adaptive noise floor. The ESP-SR AFE (AEC + VAD) replaces it;
// without AEC, barge-in is by tap, hold or wake only (§5.5)
static bool vad(const int16_t* f) {
  float db = frameDb(f);
  if (db < -100) return false;                     // digital silence (tone test, dead mic) is not the room
  bool sp = db > noiseDb + 9 && db > -60;
  if (db < noiseDb) noiseDb += 0.1f * (db - noiseDb);                 // falls fast
  else noiseDb += (sp ? 0.0005f : 0.005f) * (db - noiseDb);         // rises over ~4 s; a fan that reads as
  if (noiseDb < -90) noiseDb = -90;                                  // "speech" is absorbed in ~30 s
  return sp;
}

static void startUploadEnd(uint16_t tail = 0) { ending = true; endMark = micW; tEnd = millis(); tailMs = tail; }

static void upload(uint32_t now) {
  if (!wsUp || !startSent) {                       // still connecting: the ring holds the audio
    if (micW - micR > MIC_RING - FRAME) micR = micW - (MIC_RING - FRAME);
    return;
  }
  static int16_t f[FRAME];
  static uint8_t pkt[400];
  while (micW - micR >= FRAME) {
    if (ending && (int32_t)(micR - endMark) >= 0) break;
    for (int i = 0; i < FRAME; i++) f[i] = mic[(micR + i) & (MIC_RING - 1)];
    micR += FRAME;
    if (toneTest)                                  // 1 s of 1 kHz at -12 dBFS, then silence for the VAD
      for (int i = 0; i < FRAME; i++) f[i] = upFrames < 50 ? 8192 * sinf(TWO_PI * 1000 * i / 16000) : 0;
    int nb = opus_encode(enc, f, FRAME, pkt, sizeof pkt);
    if (nb > 0) ws.sendBIN(pkt, nb);
    upFrames++;
    if (ending) continue;
    if (vad(f)) { speechRun++; silentRun = 0; } else { silentRun++; if (speechRun < 13) speechRun = 0; }
    if (speechRun >= 13) speechSeen = true;        // >= 250 ms of speech
    if (ptt && held) {
      if (now - startAt > cal.pttMaxMs) startUploadEnd();
    } else if (speechSeen && silentRun * 20 >= cal.vadTailMs) startUploadEnd(cal.vadTailMs);
    else if (!speechSeen && upFrames >= 200) { endTurn(TI_NO_SPEECH); return; }   // 4 s, nothing said
    if (upFrames >= 2750) startUploadEnd();        // 55 s: the gateway's hard limit is 60 s
  }
  if (ending && (int32_t)(micR - endMark) >= 0) {
    sendMarker(false);
    S.uploading = false;
    setState(T_WAITING);
    post(TE_THINK);
  }
}

static void request(uint8_t r, uint32_t now) {
  switch (r) {
    case RQ_START: case RQ_START_PTT:
      if (st == T_PLAYING || st == T_DRAINING) {   // talking over the reply is a barge-in
        request(RQ_BARGE, now);
        ptt = held = r == RQ_START_PTT;
        return;
      }
      if (st != T_IDLE && st != T_FOLLOWUP) return;   // one turn at a time
      ptt = held = r == RQ_START_PTT;
      openTurn(micW - MS(cal.prerollMs));
      break;
    case RQ_RELEASE:
      held = false;
      if ((st == T_UPLOADING || st == T_CONNECTING) && speechSeen && !ending) startUploadEnd();   // <= 40 ms
      break;
    case RQ_BARGE:
      if (st != T_PLAYING && st != T_DRAINING) return;
      fade(MS(30));
      ptt = held = false;
      netEventInterrupt();
      bargeMark = micW - MS(cal.prerollMs);
      S.uploading = true;
      post(TE_LISTEN);
      setState(T_BARGEIN);
      break;
    case RQ_STOP:
      fade(MS(8));
      if (st == T_BARGEIN) stopSeen = true;
      else if (st == T_WAITING || st == T_PLAYING || st == T_DRAINING) endTurn(TI_STOPPED);
      break;
    case RQ_GRANDMA:
      grandma = true;
      break;
    case RQ_TEST:
      if (st != T_IDLE && st != T_FOLLOWUP) return;
      ptt = held = false;
      openTurn(micW);
      toneTest = true;
      break;
  }
}

static void step(uint32_t now) {
  switch (st) {
    case T_IDLE:
    case T_FOLLOWUP: {
      static int16_t f[FRAME];
      if (micW - micR > MIC_RING) micR = micW - FRAME * 2;
      while (micW - micR >= FRAME) {               // keep the noise floor current; follow-up onset
        for (int i = 0; i < FRAME; i++) f[i] = mic[(micR + i) & (MIC_RING - 1)];
        micR += FRAME;
        bool sp = vad(f);
        onsetRun = sp ? onsetRun + 1 : 0;
        if (!sfxBusy()) cryFrame(f, noiseDb, now);   // no AEC yet: never score our own sounds
        if (st == T_FOLLOWUP && onsetRun >= 3) {   // 60 ms of speech: upload with pre-roll (§5.6)
          ptt = held = false;
          openTurn(micW - MS(cal.prerollMs));
          return;
        }
      }
      uint32_t win = grandma ? cal.followupGrandmaMs : cal.followupMs;
      if (st == T_FOLLOWUP && now - stateAt > win) { grandma = false; setState(T_IDLE); post(TE_IDLE, TI_DONE); }
      break;
    }
    case T_CONNECTING:
      if (wsUp && startSent) setState(T_UPLOADING);
      else if (now - stateAt > 5000) { Serial.println("[turn] audio socket did not open"); endTurn(TI_ERROR); return; }
      upload(now);
      break;
    case T_UPLOADING:
      if (wsClosed) { Serial.println("[turn] socket closed during upload"); endTurn(TI_ERROR); return; }
      upload(now);
      break;
    case T_WAITING:
      if (remoteEnd) { post(TE_REPLY_END, 1); endTurn(TI_DONE); return; }   // an empty reply: audio_end, no frames
      if (wsClosed) { endTurn(TI_ERROR); return; }
      if (now - stateAt > 8000) { Serial.println("[turn] no reply in 8 s"); endTurn(TI_SLOW); return; }
      break;
    case T_PLAYING:
    case T_DRAINING:
      if (!primed && !tAudible && (buffered() >= MS(cal.jitterStartMs) || now - firstPktAt >= 150 || remoteEnd)) {
        primed = true;                             // the jitter buffer is full enough: playback starts
        tAudible = now;
      }
      if (st == T_PLAYING && remoteEnd) setState(T_DRAINING);
      if (st == T_PLAYING && wsClosed) {           // closed without audio_end: say fails
        post(TE_REPLY_END, 0);
        endTurn(TI_STOPPED);
        return;
      }
      if (st == T_PLAYING && primed && plcLeft && buffered() < FRAME && now - lastPktAt > 20) {
        static int16_t tmp[FRAME];                 // late frame: Opus concealment, max 60 ms
        int got = opus_decode(dec, nullptr, 0, tmp, FRAME, 0);
        uint32_t w = pcmW.load(std::memory_order_relaxed);
        for (int i = 0; i < got; i++) pcm[(w + i) & (PCM_RING - 1)] = tmp[i];
        pcmW.store(w + max(got, 0), std::memory_order_release);
        plcLeft--;
        lastPktAt = now;
      }
      // drained, or a stopped speaker (sleep) never draining it: 2 s cap
      if (st == T_DRAINING && (buffered() == 0 || now - stateAt > 2000)) {
        if (buffered()) fade(MS(8)); else primed = false;
        closeWs();
        // §5.8 budget: end of speech -> first audio, 1.0 s typical / 1.6 s worst (+ ~40 ms of I2S DMA)
        Serial.printf("[turn] latency%s: socket %lu ms | speech end -> audio_end %u ms | audio_end -> first frame %lu ms"
                      " | first frame -> playback %lu ms | speech end -> playback %lu ms\n", toneTest ? " (tone test)" : "",
                      (unsigned long)(tConn - startAt), tailMs, (unsigned long)(firstPktAt - tEnd),
                      (unsigned long)(tAudible - firstPktAt), (unsigned long)(tAudible - tEnd + tailMs));
        post(TE_REPLY_END, 1);
        setState(T_FOLLOWUP);
        post(TE_FOLLOWUP, grandma);
      }
      break;
    case T_BARGEIN:
      // the new socket opens only after the old turn ends, or it would replace it in the gateway (§5.5)
      if (remoteEnd || wsClosed || stopSeen || now - stateAt > 1500) {
        post(TE_REPLY_END, remoteEnd);
        uint32_t from = bargeMark;
        openTurn(from);
      }
      break;
  }
}

static void turnTask(void*) {
  for (;;) {
    uint32_t now = millis();
    uint8_t r;
    while (xQueueReceive(reqQ, &r, 0) == pdTRUE) request(r, now);
    if (wsOpen) ws.loop();
    step(millis());
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ===========================================================================
void turnBegin() {
  mic = (int16_t*)heap_caps_calloc(MIC_RING, 2, MALLOC_CAP_SPIRAM);
  pcm = (int16_t*)heap_caps_calloc(PCM_RING, 2, MALLOC_CAP_SPIRAM);
  int e1, e2;
  enc = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &e1);
  dec = opus_decoder_create(16000, 1, &e2);
  if (!mic || !pcm || !enc || !dec) { Serial.printf("[turn] init failed (opus %d/%d)\n", e1, e2); return; }
  opus_encoder_ctl(enc, OPUS_SET_BITRATE(cal.opusKbps * 1000));
  opus_encoder_ctl(enc, OPUS_SET_VBR(1));
  opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(cal.opusComplexity));
  reqQ = xQueueCreate(8, 1);
  evQ = xQueueCreate(16, sizeof(TurnEvent));
  xTaskCreatePinnedToCore(turnTask, "turn", 16384, nullptr, 15, nullptr, 0);   // opus_enc + WS (§3.3)
  Serial.printf("[turn] ready, audio url %s\n", settings.audioUrl[0] ? settings.audioUrl : "(not set)");
}

static void req(uint8_t r) { if (reqQ) xQueueSend(reqQ, &r, 0); }

bool turnStart(bool pushToTalk) {
  if (!reqQ || !settings.audioUrl[0] || !S.wifiUp) return false;
  req(pushToTalk ? RQ_START_PTT : RQ_START);
  return true;
}
void turnRelease()   { req(RQ_RELEASE); }
void turnBargeIn()   { req(RQ_BARGE); }
void turnStopAudio() { req(RQ_STOP); }
void turnGrandma()   { req(RQ_GRANDMA); }
TurnState turnState() { return st; }
bool turnNext(TurnEvent& e) { return evQ && xQueueReceive(evQ, &e, 0) == pdTRUE; }

// `turn test` (§14.3): the offline checks, then one real turn that uploads a 1 kHz tone
bool turnTest(Print& out) {
  bool tls, ok = true;
  char h[32];
  uint16_t p;
  const char* path;
  ok &= parseUrl("ws://192.168.1.5:8081", tls, h, sizeof h, p, path) && !tls && !strcmp(h, "192.168.1.5") && p == 8081 && !strcmp(path, "/");
  ok &= parseUrl("wss://gw.example.com/audio/v2", tls, h, sizeof h, p, path) && tls && !strcmp(h, "gw.example.com") && p == 443 && !strcmp(path, "/audio/v2");
  ok &= parseUrl("ws://h:9/x", tls, h, sizeof h, p, path) && p == 9 && !strcmp(path, "/x");
  ok &= !parseUrl("http://h", tls, h, sizeof h, p, path) && !parseUrl("ws://", tls, h, sizeof h, p, path);
  out.printf("turn url parser: %s | state %s | mic %lu samples | noise %.0f dBFS\n", ok ? "PASS" : "FAIL",
             TURN_STATES[st], (unsigned long)micW.load(), noiseDb);
  ok &= crySelfTest(out);
  if (!reqQ || !settings.audioUrl[0] || !S.wifiUp) out.println("tone turn: skipped (offline or no audio url)");
  else { req(RQ_TEST); out.println("tone turn: started, watch for the [turn] latency line"); }
  return ok;
}
