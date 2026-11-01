// NVS: identity, settings, the [CAL] blob, the last epoch, the cry queue
// and the brownout log (FW-SOT §13.3). One Preferences handle, opened at boot.
#pragma once
#include <Arduino.h>
#include "../../Config.h"

struct Settings {
  char    deviceId[40];
  char    token[96];          // logged as "set" / "not set", never printed
  char    wifiSsid[33];
  char    wifiPass[65];
  char    mqttUrl[128];       // mqtt://<LAN-IP>:1883 (dev) or mqtts://...:8883
  char    audioUrl[128];      // ws://<LAN-IP>:8081 (dev) or wss://...
  uint8_t volume;             // 0-100, config.volume / volume.level
  char    quietStart[6];      // "HH:MM", "" = no quiet hours
  char    quietEnd[6];
  bool    movementOn;         // wheels only (§7.7, G9)
  char    language[16];
  char    childId[64];        // status.active_child_id
  bool    childSet;           // false: active_child_id is null
  uint8_t mode;               // last set_mode (sleep is never restored)
};

extern Settings settings;
extern Cal cal;

void storeBegin();
void settingsSave();

void calSave();
void calReset();
void calShow(Print& out);
bool calSet(const char* name, const char* values);   // arrays take N space-separated values

uint32_t storeEpochLoad();
void     storeEpochSave(uint32_t epoch);

// Cry events persist until PUBACKed, so a parent alert survives a reboot (§4.5).
#define CRYQ_MAX   20
#define CRYQ_SLOT  200
int  cryQueueLoad(char (*out)[CRYQ_SLOT], int max);
void cryQueueAdd(const char* payload);
void cryQueueRemove(const char* eventId);

// Brownout boots: approximate epoch of each, newest first (§2.3).
void brownoutLog(uint32_t epoch);
bool brownoutTwiceWithin(uint32_t seconds);
void brownoutClear();
