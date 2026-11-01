// ============================================================
// Xelulu firmware: ESP32-S3-DevKitC-1 N16R8.
// Built from Xelulu_Firmware_Source_of_Truth.md (FW-SOT v1.2) against
// Document B v2 (proto_ver 2) and Document C v1.0, xelulu@79a92d0.
//
// Build: Arduino IDE 2.x / arduino-cli, core esp32 3.3.10
//   ESP32S3 Dev Module · Flash 16 MB · QIO 80 MHz · PSRAM: OPI ·
//   USB CDC on boot: Enabled · USB mode: Hardware CDC and JTAG ·
//   Partition scheme: Custom (partitions.csv in this folder)
//   FQBN esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,CDCOnBoot=cdc,USBMode=hwcdc,FlashMode=qio,PartitionScheme=custom
// Libraries: TFT_eSPI 2.5.43 (configured by build_opt.h in this folder,
//   its shared User_Setup.h is left alone), ArduinoJson 7, VL53L0X (Pololu) 1.3.1,
//   WebSockets (links2004) 2.7.2, libopus (pschatzmann/arduino-libopus @bae0f85).
// Console: native USB, 115200. `help` lists every command (§14.3).
//
// Tasks (§3.3): audio_io c1/22 · body c1/21 · sense c0/17 · turn c0/15 · net c0/12 ·
// housekeeping c0/5 · loopTask c1/1 (behaviour + face). Nothing in an ISR.
// ============================================================
#include <Arduino.h>
#include "Pins.h"
#include "src/State.h"
#include "src/store/Store.h"
#include "src/face/Face.h"
#include "src/audio/Audio.h"
#include "src/audio/Turn.h"
#include "src/sense/Sense.h"
#include "src/body/Body.h"
#include "src/body/Power.h"
#include "src/net/Net.h"
#include "src/ota/Ota.h"
#include "src/behavior/Behavior.h"
#include "src/console/Console.h"
#include "esp_task_wdt.h"

SET_LOOP_TASK_STACK_SIZE(16 * 1024);

static void housekeeping(void*) {
  for (;;) {
    uint32_t now = millis();
    faultTick(now);              // proto_mismatch / cmd_invalid clear after 60 s
    powerTick(now);              // battery estimate from the servo-reported rail
    netHousekeeping(now);        // epoch persistence, one error event per new hardware fault
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void setup() {
  // FIRST: STBY is tied high, so the six motor pins low are what keeps the wheels still (§2.4)
  for (uint8_t p : {PIN_MOT_L_PWM, PIN_MOT_L_IN1, PIN_MOT_L_IN2, PIN_MOT_R_PWM, PIN_MOT_R_IN1, PIN_MOT_R_IN2}) {
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);      // never block on a USB console nobody is reading
  for (uint32_t t = millis(); !Serial && millis() - t < 1500;) delay(10);
  Serial.printf("\nXelulu fw %s  FW-SOT %s  proto_ver %d  reset %d\n", FW_VER, FW_SOT_VER, PROTO_VER, esp_reset_reason());

  storeBegin();
  faceBegin();
  faceSet(FACE_SMILE);           // something on the panel while the rest comes up
  audioBegin();
  turnBegin();                   // voice turn: WS + Opus, core 0 (§5.2)
  senseBegin();
  bodyBegin();                   // servo scan + 6.2 V check before any torque (§12 boot)
  netBegin();
  otaBegin();
  powerBoot(netApproxBootEpoch());
  behaviorBegin();
  xTaskCreatePinnedToCore(housekeeping, "housekeeping", 4096, nullptr, 5, nullptr, 0);
  enableLoopWDT();               // the task watchdog covers loopTask too (§3.3)
}

void loop() {
  uint32_t now = millis();       // ONE clock reading per loop, passed down (Eillik debug.md §4a)
  consoleLoop(now);
  behaviorLoop(now);
}
