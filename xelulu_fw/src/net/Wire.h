// Document B v2 codec: strict command parsing (mirrors the contract's zod
// schemas: exact key sets, ints stay ints, closed lists case-sensitive) and
// builders for every upstream message. No Arduino dependency on purpose.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../../Contract.h"

// config delta keys present
#define CFG_VOLUME   0x01
#define CFG_QUIET    0x02
#define CFG_MOVEMENT 0x04
#define CFG_LANGUAGE 0x08
#define CFG_CHILD    0x10

struct Cmd {
  char     cmdId[64];
  uint8_t  action;                 // CmdAction; A_UNKNOWN = valid envelope, unknown action
  // say / animate
  char     expression[32];
  char     lang[16];
  uint8_t  voice;
  uint32_t durationHintMs;
  char     eyes[16];
  bool     loop;
  // gesture
  char     name[32];
  // move
  uint8_t  moveCmd;
  float    linearMms, angularMradS, maxDistMm;
  // play
  char     contentId[64];
  uint8_t  source;
  // set_mode
  uint8_t  mode;
  // led
  uint8_t  ledTarget, ledEffect, brightness;
  uint32_t rgb;
  // volume (and config.volume)
  uint8_t  level;
  // config
  uint8_t  cfg;                    // CFG_* present
  char     quietStart[6], quietEnd[6];
  bool     movementOn;
  char     language[16];
  char     childId[64];
  bool     childNull;
  // ota
  char     manifestUrl[256];
};

enum ParseResult : uint8_t {
  PARSE_OK,
  PARSE_UNKNOWN_ACTION,   // valid envelope, action not in CMD_ACTIONS -> ack rejected/unknown_action
  PARSE_NOT_JSON,         // not JSON / not an object             -> no ack, fault cmd_invalid
  PARSE_PROTO,            // proto_ver missing or != 2            -> no ack, fault proto_mismatch
  PARSE_INVALID,          // missing / extra / mistyped field     -> no ack, fault cmd_invalid
};
ParseResult wireParseCmd(const char* json, size_t len, Cmd& out, const char** why = nullptr);

// Builders: JSON into buf, return its length (0 if it does not fit). ts = unix seconds.
size_t wireHello(char* buf, size_t n, uint32_t ts, const char* deviceId, const char* fwVer, const char* auth);
size_t wireStatus(char* buf, size_t n, uint32_t ts, uint8_t batteryPct, uint8_t mode, int rssi,
                  const char* const* faults, uint8_t nFaults, const char* childId /* nullptr = null */, int tempC);
size_t wireAck(char* buf, size_t n, uint32_t ts, const char* cmdId, uint8_t status, uint8_t reason);
size_t wireAudioMarker(char* buf, size_t n, uint32_t ts, bool start, const char* turnId);

// Events. timeUncertain is written only when true (it is optional on the wire).
size_t wireEventTouch(char* buf, size_t n, uint32_t ts, const char* id, bool tu, uint8_t wireZone, uint8_t gesture);
size_t wireEventWake(char* buf, size_t n, uint32_t ts, const char* id, bool tu, uint8_t source, int dirDeg /* <0: omitted */);
size_t wireEventCry(char* buf, size_t n, uint32_t ts, const char* id, bool tu, float confidence, float durationS);
size_t wireEventMotion(char* buf, size_t n, uint32_t ts, const char* id, bool tu, uint8_t kind, float value);
size_t wireEventButton(char* buf, size_t n, uint32_t ts, const char* id, bool tu, uint8_t action);
size_t wireEventInterrupt(char* buf, size_t n, uint32_t ts, const char* id, bool tu);
size_t wireEventError(char* buf, size_t n, uint32_t ts, const char* id, bool tu, const char* code, const char* detail);

// "ev_" / "t_" + 16 random bytes as base64url (22 chars): same shape as the simulator's ids
void wireRandomId(char* out, const char* prefix, const uint8_t random16[16]);
// Document C pairing code: FNV-1a over device_id NUL fw_ver, 6 chars of Crockford base32 sans vowels
void wirePairingCode(const char* deviceId, const char* fwVer, char out[7]);
