// The voice turn (FW-SOT §5.2-5.6): mic ring -> VAD -> Opus -> WS up,
// WS down -> Opus -> PCM ring -> mixer. One turn at a time, one socket per
// turn. The `turn` task (core 0, priority 15) owns the socket and BOTH Opus
// states: this libopus build keeps one global pseudo-stack, so encode and
// decode must never run on two tasks.
#pragma once
#include <Arduino.h>

enum TurnState : uint8_t {
  T_IDLE, T_CONNECTING, T_UPLOADING, T_WAITING, T_PLAYING, T_DRAINING, T_FOLLOWUP,
  T_BARGEIN,              // the child talked over Xelulu: waiting for the old turn to end (§5.5)
};
extern const char* const TURN_STATES[];

// Turn -> behaviour (loopTask), in order. a = detail below.
enum TurnEventKind : uint8_t {
  TE_LISTEN,              // mic audio is (about to be) leaving: LISTENING + blue glow
  TE_THINK,               // audio_end sent
  TE_SPEAK,               // first reply frame
  TE_REPLY_END,           // a = 1 reply finished (audio_end + drained), 0 socket closed without audio_end
  TE_FOLLOWUP,            // window open (§5.6)
  TE_IDLE,                // turn over; a = TurnEnd
};
enum TurnEnd : uint8_t { TI_DONE, TI_NO_SPEECH, TI_SLOW, TI_ERROR, TI_STOPPED };
struct TurnEvent { uint8_t kind, a; };

void      turnBegin();
bool      turnStart(bool pushToTalk);   // wake / hold. While PLAYING it is a barge-in. false: offline or no URL
void      turnRelease();                // push-to-talk released
void      turnBargeIn();                // head/belly tap during the reply
void      turnStopAudio();              // cloud stop_audio: 8 ms fade, flush, close
void      turnGrandma();                // say.voice = grandma: 8 s follow-up after this reply
TurnState turnState();
bool      turnNext(TurnEvent& e);       // behaviour only
bool      turnTest(Print& out);         // `turn test`: url + cry checks, then a 1 kHz tone turn with latency

// audio_io task only
void turnMicIn(const int16_t* pcm, int n);   // 16 kHz mono
bool turnStreamOut(int16_t* out, int n);     // false (and zeros) while no reply is playing
