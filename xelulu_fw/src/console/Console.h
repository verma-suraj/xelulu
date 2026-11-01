// Serial bring-up console (FW-SOT §14.3), extending Eillik's "type 1-38".
// Native USB, 115200. `help` lists everything.
#pragma once
#include <Arduino.h>

void consoleLoop(uint32_t now);
