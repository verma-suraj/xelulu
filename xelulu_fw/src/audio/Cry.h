// Cry detector, bring-up heuristic (FW-SOT §5.10): loud frames whose pitch
// sits in the infant-cry band (250-600 Hz) with strong harmonicity, scored
// per second, averaged over cryMinS seconds. NOT FOR RELEASE: P0 replaces it
// with the TinyML classifier. Runs in the turn task while no turn is open,
// sends only confidence and duration, never keeps audio (§5.9).
#pragma once
#include <Arduino.h>

void cryFrame(const int16_t* f, float noiseDb, uint32_t now);   // one 20 ms frame (320 samples)
void cryPause();                                                 // a turn opened: restart the window
bool crySelfTest(Print& out);                                    // `turn test`: pitch check on synthetic frames
