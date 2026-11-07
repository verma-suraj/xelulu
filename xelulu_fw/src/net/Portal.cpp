#include "Portal.h"
#include "Net.h"
#include "../State.h"
#include "../store/Store.h"
#include "../ota/Ota.h"
#include "../../Config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>
#include <HTTPUpdateServer.h>
#include <Update.h>
#include <StreamString.h>
#include <TFT_eSPI.h>
#include "esp_flash.h"

static_assert(sizeof NET_ADMIN_PASS >= 9 && sizeof NET_ADMIN_PASS <= 64,
              "Config.h NET_ADMIN_PASS: 8 to 63 characters (it is also the setup hotspot's WPA2 key)");

static WebServer web(80);
static DNSServer dns;                   // async in core 3.x: answers every name with the hotspot itself
static HTTPUpdateServer updater;
static volatile bool apOn = false;
static bool serving = false, uploading = false, flashSmall = false;
static uint32_t lastSta = 0, nextTry = 0;
static volatile uint32_t otaKb = 0;
static String nets;                     // <option>s: the networks seen when the hotspot opened

static void hostOf(const char* url, char* out, size_t n) {   // "mqtt://192.168.1.50:1883" -> "192.168.1.50"
  const char* p = strstr(url, "://");
  p = p ? p + 3 : url;
  snprintf(out, n, "%.*s", (int)strcspn(p, ":/"), p);
}

// ===========================================================================
// status on the face (Face.cpp calls faceOverlay before every push). Written
// here at 2 Hz into the buffer the face is not reading, then flipped.
// ===========================================================================
enum Slot : uint8_t { TOP, LOW1, LOW2, SLOTS };
static const uint8_t SLOT_Y[SLOTS] = {41, 166, 183};   // inside the 1.5x zoom's view: x 13-226, y 40-199
static char screen[2][SLOTS][34];
static volatile uint8_t shown = 0;

void faceOverlay(TFT_eSprite& s) {
  char (*l)[34] = screen[shown];
  s.setTextDatum(TL_DATUM);
  s.setTextColor(TFT_WHITE, TFT_BLACK);
  for (uint8_t i = 0; i < SLOTS; i++) {
    if (!l[i][0]) continue;
    uint8_t font = s.textWidth(l[i], 2) <= 208 ? 2 : 1;   // the small 6x8 font when a line is too wide
    s.fillRect(14, SLOT_Y[i] - 1, 212, 17, TFT_BLACK);
    s.drawString(l[i], 16, SLOT_Y[i] + (font == 1 ? 4 : 0), font);
  }
}

static void statusTick(uint32_t now) {
  static uint32_t upSince = 0;
  if (!S.mqttUp) upSince = 0;
  else if (!upSince) upSince = now;
  uint8_t w = shown ^ 1;
  char (*l)[34] = screen[w];
  for (uint8_t i = 0; i < SLOTS; i++) l[i][0] = 0;
  if (otaRunning()) {
    strcpy(l[TOP], "Updating firmware");
    if (uploading) snprintf(l[LOW2], 34, "%lu KB received", (unsigned long)otaKb);
  } else if (apOn) {
    strcpy(l[TOP], "Wi-Fi setup");
    snprintf(l[LOW1], 34, "Join Wi-Fi %s", netHost());
    snprintf(l[LOW2], 34, "then open %s", WiFi.softAPIP().toString().c_str());
  } else if (!WiFi.isConnected()) {
    strcpy(l[TOP], "Wi-Fi: joining");
    snprintf(l[LOW1], 34, "%s", settings.wifiSsid);
  } else if (!S.mqttUp) {
    char h[sizeof settings.mqttUrl];
    hostOf(settings.mqttUrl, h, sizeof h);
    const char* why = netLinkName();
    snprintf(l[TOP], 34, "Wi-Fi ok %s", WiFi.localIP().toString().c_str());
    snprintf(l[LOW1], 34, "Server %s", h[0] ? h : "not set");
    snprintf(l[LOW2], 34, "%s%s", why, strcmp(why, "kicked off") ? "" : " (simulator on?)");
  } else if (now - upSince < 20000) {
    snprintf(l[TOP], 34, "Connected %s", WiFi.localIP().toString().c_str());
    snprintf(l[LOW2], 34, "pairing %s", netPairingCode());
  } else if (flashSmall) {
    strcpy(l[TOP], "Flash Size is not 16MB");
    strcpy(l[LOW2], "fix Tools, flash again");
  }
  shown = w;
}

// ===========================================================================
// web page
// ===========================================================================
static bool allowed() {   // hotspot clients already gave its WPA2 key; anyone on the LAN logs in
  return (apOn && web.client().localIP() == WiFi.softAPIP()) || web.authenticate("xelulu", NET_ADMIN_PASS);
}

static bool sameOrigin() {   // another site's page must not submit our forms with a saved login (CSRF), as /update checks
  String o = web.header("Origin");   // Origin + Host are collected for every request by updater.setup()
  return !o.length() || o == "http://" + web.header("Host");
}

static String esc(const char* s) {
  String o;
  for (; *s; s++) {
    switch (*s) {
      case '&':  o += "&amp;"; break;
      case '<':  o += "&lt;"; break;
      case '>':  o += "&gt;"; break;
      case '"':  o += "&quot;"; break;
      case '\'': o += "&#39;"; break;
      default:   o += *s;
    }
  }
  return o;
}

static String serverField() {   // just the host while the URLs are the local stack's pair, else the full URL
  char h[sizeof settings.mqttUrl], m[sizeof settings.mqttUrl + 16], a[sizeof settings.audioUrl + 16];
  hostOf(settings.mqttUrl, h, sizeof h);
  snprintf(m, sizeof m, "mqtt://%s:1883", h);
  snprintf(a, sizeof a, "ws://%s:8081", h);
  return esc(strcmp(m, settings.mqttUrl) || strcmp(a, settings.audioUrl) ? settings.mqttUrl : h);
}

static void replyAndRestart(int code, const char* type, const String& body) {
  web.client().setNoDelay(true);
  web.send(code, type, body);
  delay(100);
  web.client().stop();
  ESP.restart();
}

static void pageRoot() {
  if (!allowed()) return web.requestAuthentication();
  StreamString st;
  netStatus(st);
  const char* link = netLinkName();
  String h;
  h.reserve(4000);
  h += F("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
         "<title>Xelulu setup</title><style>body{font-family:sans-serif;max-width:32em;margin:1em auto;padding:0 1em}"
         "input{display:block;width:100%;box-sizing:border-box;padding:.5em;margin:.2em 0 .9em}"
         "button{padding:.6em 1.2em;margin:.3em 0}pre{white-space:pre-wrap;background:#eee;padding:.5em}</style><h2>Xelulu ");
  h += esc(settings.deviceId);
  h += F("</h2><p>firmware " FW_VER " &middot; pairing code ");
  h += netPairingCode();
  h += F(" &middot; server: <b>");
  h += link;
  h += F("</b></p><pre>");
  h += esc(st.c_str());
  h += F("</pre>");
  if (!strcmp(link, "kicked off"))
    h += F("<p><b>Another client is connected as this device id.</b> Stop the stack's simulator: "
           "<code>docker compose stop simulator</code>.</p>");
  if (!strcmp(link, "token refused")) h += F("<p><b>The broker refused the device token.</b> It must equal the stack's DEVICE_TOKEN.</p>");
  if (!strcmp(link, "no answer"))
    h += F("<p><b>Nothing answers at the server address.</b> Check the PC's IP, that the stack runs with "
           "DEVICE_BIND_HOST=0.0.0.0, and that the PC's firewall lets ports 1883 and 8081 in.</p>");
  h += F("<form method=post action=/save>Wi-Fi name (2.4 GHz only)<input name=ssid list=nets value='");
  h += esc(settings.wifiSsid);
  h += F("'><datalist id=nets>");
  h += nets;
  h += F("</datalist>Wi-Fi password<input name=pass type=password placeholder='empty = keep the saved one'>"
         "Server: the stack PC's IP address (ipconfig), or a full mqtt:// URL<input name=mqtt value='");
  h += serverField();
  h += F("'>Audio URL (only with a full mqtt:// URL)<input name=audio value='");
  h += esc(settings.audioUrl);
  h += F("'>Device id (DEVICE_ID)<input name=id value='");
  h += esc(settings.deviceId);
  h += F("'>Device token (DEVICE_TOKEN)<input name=tok type=password placeholder='empty = keep the saved one'>"
         "<button>Save and restart</button></form><p><a href=/update>Upload new firmware (.bin)</a></p>"
         "<form method=post action=/restart><button>Restart (says hello to the server again)</button></form>");
  web.send(200, "text/html", h);
}

static bool idOk(const String& s) {      // it names the MQTT topics
  if (s.length() >= sizeof settings.deviceId) return false;
  for (size_t i = 0; i < s.length(); i++)
    if (!isalnum((uint8_t)s[i]) && s[i] != '_' && s[i] != '-') return false;
  return true;
}

static bool tokenOk(const String& s) {   // it goes into hello's JSON and the audio socket's HTTP header
  if (s.length() >= sizeof settings.token) return false;
  for (size_t i = 0; i < s.length(); i++)
    if (s[i] <= ' ' || s[i] > '~' || s[i] == '"' || s[i] == '\\') return false;
  return true;
}

static void pageSave() {
  if (!allowed()) return web.requestAuthentication();
  if (!sameOrigin()) { web.send(403, "text/plain", "wrong origin"); return; }
  String ssid = web.arg("ssid"), pass = web.arg("pass"), mqtt = web.arg("mqtt"), audio = web.arg("audio"),
         id = web.arg("id"), tok = web.arg("tok");
  mqtt.trim(); audio.trim(); id.trim(); tok.trim();   // never the Wi-Fi name or password: spaces are legal there
  const char* bad = nullptr;
  if (ssid.length() > 32) bad = "Wi-Fi name: 32 characters at most";
  else if (pass.length() > 64) bad = "Wi-Fi password: 64 characters at most";
  else if (!idOk(id)) bad = "Device id: letters, digits, _ and - only";
  else if (!tokenOk(tok)) bad = "Device token: no spaces or quotes, 95 characters at most";
  else if (mqtt.length() && !settingsServer(mqtt.c_str(), audio.c_str()))   // writes only when valid, so last
    bad = "Server: type the PC's IP address (like 192.168.1.50), or a full mqtt:// URL plus a ws:// audio URL";
  if (bad) { web.send(400, "text/plain", bad); return; }
  bool newSsid = ssid.length() && ssid != settings.wifiSsid;
  if (ssid.length()) strlcpy(settings.wifiSsid, ssid.c_str(), sizeof settings.wifiSsid);
  if (newSsid || pass.length()) strlcpy(settings.wifiPass, pass.c_str(), sizeof settings.wifiPass);   // new name + empty = open network
  if (id.length()) strlcpy(settings.deviceId, id.c_str(), sizeof settings.deviceId);
  if (tok.length()) strlcpy(settings.token, tok.c_str(), sizeof settings.token);
  settingsSave();
  Serial.printf("[setup] saved on the web page: wifi \"%s\", server %s, id %s - restarting\n", settings.wifiSsid,
                settings.mqttUrl, settings.deviceId);
  String body = F("<meta name=viewport content='width=device-width'><p>Saved. Xelulu restarts now and joins <b>");
  body += esc(settings.wifiSsid);
  body += F("</b>. If it cannot, this setup hotspot comes back in about 30 s.</p>");
  replyAndRestart(200, "text/html", body);
}

static void pageRestart() {
  if (!allowed()) return web.requestAuthentication();
  if (!sameOrigin()) { web.send(403, "text/plain", "wrong origin"); return; }
  replyAndRestart(200, "text/plain", "Restarting");
}

static void pageOther() {   // on the hotspot every name lands here: send phones' captive-portal checks to the form
  if (!apOn) { web.send(404, "text/plain", "not found"); return; }
  web.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
  web.send(302, "text/plain", "");
}

// ===========================================================================
// setup hotspot
// ===========================================================================
static void apStart(uint32_t now) {
  WiFi.setAutoReconnect(false);
  WiFi.disconnect();                    // no STA retries under the hotspot (they hop channels): one quiet try every 2 min
  int n = WiFi.scanNetworks();          // the form's network list, while the radio is free
  nets = "";
  for (int i = 0; i < n && i < 20; i++) {
    nets += F("<option value='");
    nets += esc(WiFi.SSID(i).c_str());
    nets += F("'>");
  }
  WiFi.scanDelete();
  WiFi.softAP(netHost(), NET_ADMIN_PASS);
  dns.start(53, "*", WiFi.softAPIP());
  apOn = true;
  nextTry = now + 120000;
  Serial.printf("[setup] no Wi-Fi: join the hotspot \"%s\" (password: Config.h NET_ADMIN_PASS), then open http://%s/\n",
                netHost(), WiFi.softAPIP().toString().c_str());
}

static void apStop() {
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.setAutoReconnect(true);
  apOn = false;
  Serial.println("[setup] Wi-Fi joined: hotspot closed");
}

// ===========================================================================
static void portalTask(void*) {
  uint32_t tickAt = 0;
  for (;;) {
    uint32_t now = millis();
    bool sta = WiFi.isConnected();
    if (sta) lastSta = now;
    if (!apOn && !sta && !otaRunning() && (!settings.wifiSsid[0] || now - lastSta > 30000)) apStart(now);
    else if (apOn && sta) apStop();
    else if (apOn && settings.wifiSsid[0] && !WiFi.softAPgetStationNum() && (int32_t)(now - nextTry) >= 0) {
      nextTry = now + 120000;
      Serial.printf("[setup] nobody on the hotspot: trying \"%s\" again\n", settings.wifiSsid);
      WiFi.begin(settings.wifiSsid, settings.wifiPass);
    }
    if (!serving && WiFi.getMode() != WIFI_MODE_NULL) {   // the TCP/IP stack exists once Wi-Fi has a mode
      web.begin();
      ArduinoOTA.begin();               // also mDNS: http://<netHost()>.local/
      serving = true;
    }
    if (serving) {
      web.handleClient();               // a firmware upload runs inside this call (and reboots when good)
      ArduinoOTA.handle();              // the same for an Arduino IDE network upload
    }
    if (uploading && !Update.isRunning()) {
      uploading = false;
      otaLocal(false);
      Serial.println("[ota] Wi-Fi upload did not finish: the old firmware stays");
    }
    otaLocalCheck(now);
    if (now - tickAt >= 500) { tickAt = now; statusTick(now); }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void portalBegin() {
  flashSmall = esp_flash_default_chip && esp_flash_default_chip->size < 16u * 1024 * 1024;
  if (flashSmall)
    Serial.printf("[ota] flashed as a %lu MB chip: Wi-Fi updates will fail. Arduino IDE: Tools > Flash Size 16MB, flash over USB again\n",
                  (unsigned long)(esp_flash_default_chip->size >> 20));
  lastSta = millis();
  web.on("/", HTTP_GET, pageRoot);
  web.on("/save", HTTP_POST, pageSave);
  web.on("/restart", HTTP_POST, pageRestart);
  web.onNotFound(pageOther);
  updater.setup(&web, "/update", "xelulu", NET_ADMIN_PASS);
  Update.onProgress([](size_t done, size_t) {   // both upload paths write through Update
    if (!uploading) { uploading = true; otaLocal(true); Serial.println("[ota] Wi-Fi upload started"); }
    otaKb = done >> 10;
  });
  ArduinoOTA.setHostname(netHost());
  ArduinoOTA.setPassword(NET_ADMIN_PASS);
  ArduinoOTA.onEnd([]() { Serial.println("[ota] Arduino IDE upload complete: restarting into it"); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[ota] Arduino IDE upload failed (error %u)\n", (unsigned)e); });
  xTaskCreatePinnedToCore(portalTask, "portal", 8192, nullptr, 3, nullptr, 0);
  Serial.printf("[setup] name %s: web page + Arduino IDE network port once Wi-Fi is up (user xelulu)\n", netHost());
}
