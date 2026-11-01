// Battery estimate and power watch with no battery-voltage pin (FW-SOT §11.5, §2.3).
// The XY-3606 holds the 5 V rail until the pack nears its dropout; the sag the
// servos report is the low-battery signal. Coarse by nature: 80 / 20 / 10 / 5.
#pragma once
#include <Arduino.h>

void powerBoot(uint32_t approxEpoch);   // brownout watch: 2 in 10 min -> low-power mode
void powerTick(uint32_t now);           // housekeeping, 10 Hz
