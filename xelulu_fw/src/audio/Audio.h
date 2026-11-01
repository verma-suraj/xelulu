// audio_io: owns I2S0 RX (mics) and I2S1 TX (speaker), and mixes (FW-SOT §5).
// Both ports run master from the same clock source at 16 kHz, so they never
// drift; the loop is paced by the mic DMA, 10 ms (160 frames) per block.
#pragma once
#include <Arduino.h>

void     audioBegin();
void     audioVolume(uint8_t level);   // 0-100 -> gain_dB = -40 + 0.4 x level, capped at volMaxDb (§10.4)
void     audioSpeaker(bool on);        // sleep stops I2S1, the only way to quiet the amp (SD is on 3.3 V)
uint32_t audioPlayheadMs();            // samples actually played / 16: the ONE sync clock (§5.11)
float    audioEnvelope();              // 0..1 mouth envelope (attack 15 ms, release 90 ms, gate -45 dBFS)
void     audioMicLevels(float& leftDb, float& rightDb);   // dBFS, smoothed (`mic level`)
