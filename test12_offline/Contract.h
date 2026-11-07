// ============================================================
// Document B v2 (proto_ver 2) strings and enums, copied VERBATIM from
//   github.com/vikas-vishw/xelulu  packages/shared/src/gateway-contract/enums.ts @ 79a92d0
// tools/contract_check.py diffs every quoted string between the BEGIN/END
// markers against that file. Run it whenever the xelulu repo moves.
// Never "fix" a value here: a contract change is a proto_ver bump (§14.1).
// ============================================================
#pragma once
#include <stdint.h>
#include <string.h>

#define PROTO_VER     2
#define TOPIC_PREFIX  "xelulu"        // topics.ts: xelulu/{device_id}/{status|event|cmd|ack}

// BEGIN enums.ts
inline constexpr const char* TOUCH_ZONES[] = {"head", "left_ear", "right_ear", "belly", "back", "left_hand", "right_hand"};
inline constexpr const char* TOUCH_GESTURES[] = {"tap", "double_tap", "hold", "pat"};
inline constexpr const char* WAKE_SOURCES[] = {"hey_lulu", "touch"};
inline constexpr const char* MOTION_KINDS[] = {"pickup", "shake", "upside_down", "drop", "tilt"};
inline constexpr const char* BUTTONS[] = {"belly_talk"};
inline constexpr const char* BUTTON_ACTIONS[] = {"press", "release"};
inline constexpr const char* MODES[] = {"idle", "play", "story", "quiet", "sleep"};
inline constexpr const char* MOVE_COMMANDS[] = {"come", "stop", "turn"};
inline constexpr const char* PLAY_SOURCES[] = {"stream", "cache"};
inline constexpr const char* LED_TARGETS[] = {"ears", "cheeks", "belly"};
inline constexpr const char* LED_EFFECTS[] = {"solid", "pulse", "rainbow"};
inline constexpr const char* SAY_VOICES[] = {"xelulu", "grandma"};
inline constexpr const char* ACK_STATUSES[] = {"accepted", "done", "rejected", "failed"};
inline constexpr const char* ACK_REASONS[] = {"picked_up", "at_edge", "charging", "quiet_hours", "low_battery", "unsafe", "busy", "unknown_action"};
inline constexpr const char* EVENT_KINDS[] = {"touch", "wake", "cry", "motion", "button", "nfc", "interrupt", "error"};
inline constexpr const char* CMD_ACTIONS[] = {"say", "animate", "gesture", "move", "play", "set_mode", "led", "volume", "config", "stop_audio", "ota"};
inline constexpr const char* KNOWN_EXPRESSIONS[] = {"happy", "love", "sleepy", "curious", "sad", "listening"};
inline constexpr const char* KNOWN_EYES[] = {"blink", "hearts"};
inline constexpr const char* KNOWN_GESTURE_NAMES[] = {"wave", "high_five", "hug", "clap", "point_left", "nod", "shake"};
// END enums.ts

// Indexes into the lists above (same order).
enum WireZone : uint8_t { WZ_HEAD, WZ_LEFT_EAR, WZ_RIGHT_EAR, WZ_BELLY, WZ_BACK, WZ_LEFT_HAND, WZ_RIGHT_HAND };
enum WireGesture : uint8_t { WG_TAP, WG_DOUBLE_TAP, WG_HOLD, WG_PAT };
enum WireWake : uint8_t { WAKE_HEY_LULU, WAKE_TOUCH };
enum WireMotion : uint8_t { WM_PICKUP, WM_SHAKE, WM_UPSIDE_DOWN, WM_DROP, WM_TILT };
enum WireButtonAction : uint8_t { BTN_PRESS, BTN_RELEASE };
enum Mode : uint8_t { MODE_IDLE, MODE_PLAY, MODE_STORY, MODE_QUIET, MODE_SLEEP };
enum MoveCmd : uint8_t { MV_COME, MV_STOP, MV_TURN };
enum PlaySource : uint8_t { SRC_STREAM, SRC_CACHE };
enum LedTarget : uint8_t { LED_EARS, LED_CHEEKS, LED_BELLY };
enum LedEffect : uint8_t { FX_SOLID, FX_PULSE, FX_RAINBOW };
enum SayVoice : uint8_t { VOICE_XELULU, VOICE_GRANDMA };
enum AckStatus : uint8_t { ACK_ACCEPTED, ACK_DONE, ACK_REJECTED, ACK_FAILED };
enum AckReason : uint8_t { R_PICKED_UP, R_AT_EDGE, R_CHARGING, R_QUIET_HOURS, R_LOW_BATTERY, R_UNSAFE, R_BUSY, R_UNKNOWN_ACTION, R_NONE = 0xFF };
enum EventKind : uint8_t { EV_TOUCH, EV_WAKE, EV_CRY, EV_MOTION, EV_BUTTON, EV_NFC, EV_INTERRUPT, EV_ERROR };
enum CmdAction : uint8_t { A_SAY, A_ANIMATE, A_GESTURE, A_MOVE, A_PLAY, A_SET_MODE, A_LED, A_VOLUME, A_CONFIG, A_STOP_AUDIO, A_OTA, A_UNKNOWN = 0xFF };

// Index of `s` in a contract list, or -1. Exact, case-sensitive (closed lists, C4).
template <size_t N>
inline int contractIndex(const char* const (&list)[N], const char* s) {
  if (!s) return -1;
  for (size_t i = 0; i < N; i++) if (strcmp(list[i], s) == 0) return (int)i;
  return -1;
}

// What THIS hardware can emit (G22: no ears; hands only if the pads are fitted).
// The ear zones stay in the contract list above; they are just never sent.
inline constexpr uint8_t ZONES_FITTED[] = {WZ_HEAD, WZ_BACK, WZ_BELLY, WZ_LEFT_HAND, WZ_RIGHT_HAND};
