// On-device safety the cloud cannot override (FW-SOT §12). These read shared
// flags only, so the command intake can call them from the MQTT task and
// answer within 50 ms. Each returns a Contract AckReason, or R_NONE.
#pragma once
#include <Arduino.h>

bool    wheelsAllowed();  // every wheel interlock passes for an in-place move (local reactions)
uint8_t refuseGesture();
uint8_t refuseMove(bool isStop);
uint8_t refusePlay();
uint8_t refuseSay();      // only outside a turn
uint8_t refuseOta();
