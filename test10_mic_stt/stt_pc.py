"""TEST 10, PC side: listen to the robot's mics, print what was said, show it on the robot's screen.

    python test10_mic_stt/stt_pc.py [--port COM10] [--model base] [--lang en]

The robot (test10_mic_stt.ino) streams 16 kHz 16-bit mono as base64 lines "A:<20 ms>". Silero VAD
(bundled with faster-whisper) finds each sentence; a 0.6 s pause ends one. Whisper turns it into text,
printed here and sent back as "T:<text>"; "S:<status>" lines show what the PC is doing.
--model tiny/base/small: bigger is more accurate and slower (base: about 1 s per sentence on this i3).
--lang auto lets Whisper pick the language (en, hi, ...). The first run downloads the model.
Needs: pip install faster-whisper pyserial
"""
import argparse
import base64
import sys
import time

import numpy as np
import serial
from faster_whisper import WhisperModel
from faster_whisper.vad import VadOptions, get_speech_timestamps

RATE = 16000


def send(ser, msg):  # the robot's font is ASCII: Hindi letters arrive as ?
    ser.write((msg.replace("\n", " ")[:250] + "\n").encode("ascii", "replace"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM10")
    ap.add_argument("--model", default="base")
    ap.add_argument("--lang", default="en")
    a = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8", errors="replace", line_buffering=True)

    print(f"loading Whisper '{a.model}'...")
    model = WhisperModel(a.model, device="cpu", compute_type="int8")
    vad = VadOptions(min_silence_duration_ms=600, speech_pad_ms=200, min_speech_duration_ms=250)

    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = a.port, 115200, 0.05
    ser.dtr = ser.rts = False  # opening must not reset the ESP32
    ser.open()
    try:
        ser.set_buffer_size(rx_size=1 << 20)  # Windows: keep the audio that arrives while Whisper reads
    except AttributeError:
        pass
    send(ser, "S:PC ready - talk to me")
    print(f"listening on {a.port}. Ctrl+C to stop.")

    buf = np.zeros(0, np.int16)  # audio since the last sentence
    rest = b""
    checked = time.time()
    while True:
        rest += ser.read(max(1, ser.in_waiting))
        *lines, rest = rest.split(b"\n")
        parts = []
        for ln in lines:
            if ln.startswith(b"A:"):
                try:
                    parts.append(np.frombuffer(base64.b64decode(ln[2:].strip()), np.int16))
                except ValueError:
                    pass  # a torn line: 20 ms lost
            elif ln.strip():
                print("robot:", ln.decode("utf-8", "replace").strip())
        if parts:
            buf = np.concatenate([buf, *parts])
        if time.time() - checked < 0.3 or len(buf) < RATE:
            continue
        checked = time.time()
        audio = buf.astype(np.float32) / 32768
        speech = get_speech_timestamps(audio, vad)
        if not speech:
            buf = buf[-RATE // 2:]  # nothing said: keep 0.5 s, a word may be starting
            continue
        start, end = speech[0]["start"], speech[0]["end"]
        if end > len(buf) - int(0.7 * RATE) and len(buf) < 30 * RATE:
            continue  # still talking
        send(ser, "S:thinking...")
        t0 = time.time()
        clip = audio[max(0, start - RATE // 5):end + RATE // 5]
        # one greedy pass: Whisper's default retries at 5 temperatures x beam 5 when noise makes it unsure,
        # which took 13-23 s a sentence on this i3 with the fan on
        segs, _ = model.transcribe(clip, language=None if a.lang == "auto" else a.lang, beam_size=1,
                                   temperature=0.0, without_timestamps=True, vad_filter=False,
                                   condition_on_previous_text=False)
        text = " ".join(s.text.strip() for s in segs if s.no_speech_prob < 0.6).strip()  # drop fan "words"
        buf = buf[end:]
        if text:
            print(f"YOU SAID: {text}   ({len(clip) / RATE:.1f} s of speech, {time.time() - t0:.1f} s to read)")
            send(ser, "T:" + text)
        send(ser, "S:listening")


if __name__ == "__main__":
    main()
