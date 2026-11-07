// Wi-Fi, clock, MQTT (Document B v2 + Document C) and the command intake.
// The net task (core 0, priority 12) does every publish except the intake's
// immediate accepted/rejected ack, which goes out from the MQTT task itself.
#pragma once
#include <Arduino.h>
#include "Wire.h"

void     netBegin();
uint32_t netEpoch(bool* uncertain = nullptr);   // unix s; uncertain until the first SNTP sync since boot
uint32_t netApproxBootEpoch();                  // best guess at boot (RTC memory, else NVS)
void     netHousekeeping(uint32_t now);         // 10 Hz from housekeeping: epoch, error events
void     netReconnect();                        // after the console changes wifi / id / urls

// Events: stamped now (ts, event_id, time_uncertain), published in order, one in flight (§4.5)
void netEventTouch(uint8_t wireZone, uint8_t gesture);
void netEventWake(uint8_t source, int16_t dirDeg);   // dirDeg < 0: unknown, omitted
void netEventCry(float confidence, float durationS);
void netEventMotion(uint8_t kind, float value);
void netEventButton(uint8_t action);
void netEventInterrupt();
void netEventError(const char* code, const char* detail);

// Terminal acks from the behaviour engine (done / failed). Recorded for idempotency.
void netAck(const char* cmdId, uint8_t status, uint8_t reason = R_NONE);
bool netNextCmd(Cmd& c);                        // accepted commands, behaviour only

void        netStatus(Print& out);
const char* netPairingCode();
const char* netHost();                          // "xelulu-a1b2c3" from the MAC: hotspot, mDNS and DHCP name
const char* netLinkName();                      // the broker link in a few words, for the face and the web page
bool        netSelfTest(Print& out);            // `selftest wire`: the contract fixtures, on target
