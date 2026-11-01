// OTA via the `ota` command (FW-SOT §13.4). The manifest format is not in the
// contract (G12); interim fields: version, url, sha256, size.
#pragma once
#include <Arduino.h>

void otaBegin();                 // notes whether this boot is a new, unverified image
void otaMarkValid();             // hello + first status worked: keep this image
bool otaStart(const char* cmdId, const char* manifestUrl);   // acks done/failed itself
bool otaRunning();
