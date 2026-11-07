// OTA via the `ota` command (FW-SOT §13.4). The manifest format is not in the
// contract (G12); interim fields: version, url, sha256, size.
// Plus uploads over Wi-Fi (net/Portal.cpp: the web page and the Arduino IDE network port).
#pragma once
#include <Arduino.h>

void otaBegin();                 // notes whether this boot is a new, unverified image
void otaMarkValid();             // hello + first status worked: keep this image
bool otaStart(const char* cmdId, const char* manifestUrl);   // acks done/failed itself
bool otaRunning();               // either kind of update is writing flash
void otaLocal(bool busy);        // a Wi-Fi upload started (true) or failed (false)
void otaLocalCheck(uint32_t now);   // an image that came by Wi-Fi is kept once Wi-Fi works, 30 s after boot
