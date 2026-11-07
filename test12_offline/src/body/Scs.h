// Feetech SCS protocol (SC09 = SCSCL family) on one half-duplex UART line.
// Own driver instead of the SCServo library: with TX -> 1 kOhm -> signal <- RX
// the ESP32 hears its own request first. SCServo's checkHead() would take that
// echo (FF FF id 04 02 ...) for the reply and return garbage for every read,
// and every feature here (done-on-feedback, rail voltage, arm-grab) is a read.
// Body task only.
#pragma once
#include <Arduino.h>

// SCSCL memory map (16-bit values are big-endian on this family)
#define SCS_ID           5
#define SCS_MAX_TORQUE  16   // EEPROM, 0-1000 (0.1 %)
#define SCS_TORQUE_EN   40
#define SCS_GOAL_POS    42   // + goal time (44) + goal speed (46)
#define SCS_LOCK        48   // EEPROM write lock
#define SCS_PRESENT     56   // pos(2) speed(2) load(2) volt(1) temp(1)

struct ScsFeedback { int16_t pos, speed, load; uint8_t volt10, temp; };   // load: -1000..1000

bool scsBegin(int8_t tx, int8_t rx);       // 1 Mbps; detects whether our bytes echo back
void scsEnd();
bool scsPing(uint8_t id);
bool scsRead(uint8_t id, uint8_t addr, uint8_t* buf, uint8_t n);
bool scsWrite(uint8_t id, uint8_t addr, const uint8_t* data, uint8_t n);   // waits for the status reply
void scsSyncWrite(uint8_t addr, uint8_t len, const uint8_t* ids, uint8_t count, const uint8_t* data);
bool scsFeedback(uint8_t id, ScsFeedback& fb);
bool scsSetId(uint8_t id, uint8_t newId);
bool scsEepromWord(uint8_t id, uint8_t addr, uint16_t value);   // unlock, write, lock
bool scsEcho();
