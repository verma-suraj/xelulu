#include "SenseBus.h"
#include "../../Pins.h"
#include "../State.h"
#include "esp_task_wdt.h"

static QueueHandle_t q;
volatile SenseDump dumpMode = DUMP_OFF;

static const char* const ZONE_NAMES[ZONES] = {"head", "back", "belly", "left_hand", "right_hand"};
const char* zoneName(uint8_t z) { return z < ZONES ? ZONE_NAMES[z] : "?"; }

void post(uint8_t kind, uint8_t a, uint8_t b, float value) {
  SenseEvent e = {kind, a, b, value, millis()};
  xQueueSend(q, &e, 0);                 // a full queue drops: behaviour is behind anyway
}

bool senseNext(SenseEvent& e) { return q && xQueueReceive(q, &e, 0) == pdTRUE; }
void senseDump(SenseDump d) { dumpMode = d; }
void senseCalImu() { imuCalReq = true; }
void senseCalTof() { tofCalReq = true; }

// ---- bus helpers (Wire has a 25 ms timeout; the idle check below keeps us off a wedged bus) ----
bool i2cWrite(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool i2cRead(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

bool muxSelect(uint8_t ch) {
  static int8_t current = -1;
  if (current == ch) return true;
  Wire.beginTransmission(0x70);
  Wire.write(1 << ch);
  bool ok = Wire.endTransmission() == 0;
  current = ok ? ch : -1;
  return ok;
}

// A wedged slave holds SDA low; a transfer that is never started cannot
// block (Eillik debug.md §2). No active recovery: reinstalling the driver
// at runtime panicked the chip there. A paused bus beats a reboot loop.
static bool busIdle() {
  return digitalRead(PIN_I2C_SDA) == HIGH && digitalRead(PIN_I2C_SCL) == HIGH;
}

static void senseTask(void*) {
  esp_task_wdt_add(nullptr);
  TickType_t last = xTaskGetTickCount();
  uint32_t tick = 0, stuckSince = 0, dumpAt = 0;
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(10));
    esp_task_wdt_reset();
    uint32_t now = millis();
    if (!busIdle()) {
      if (!stuckSince) stuckSince = now;
      else if (now - stuckSince > 1000) faultSet(F_I2C_BUS_STUCK);
      continue;                         // sensors go stale -> the body stops the wheels
    }
    if (stuckSince) { stuckSince = 0; faultClear(F_I2C_BUS_STUCK); }

    imuStep(now);                       // 100 Hz
    if (tick & 1) tofStep(now, 2, 3);   // each ToF at 50 Hz
    else { touchStep(now); tofStep(now, 0, 1); }
    if (imuCalReq) imuCalStep(now);
    if (tofCalReq) tofCalStep(now);

    if (dumpMode != DUMP_OFF && now - dumpAt >= 100) {
      dumpAt = now;
      if (dumpMode == DUMP_TOUCH) touchDumpLine();
      else if (dumpMode == DUMP_IMU) imuDumpLine();
      else tofDumpLine();
    }
    tick++;
  }
}

void senseBegin() {
  q = xQueueCreate(32, sizeof(SenseEvent));
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  Wire.setTimeOut(25);
  // presence of every device on the bus, once, for the boot log
  bool touchOk = touchInit();
  bool imuOk = imuInit();
  uint8_t id;
  bool baro = i2cRead(0x77, 0xD0, &id, 1) && id == 0x55;
  Serial.printf("[sense] MPR121 %s, MPU6050 %s, BMP180 %s (unused), HMC5883L not used\n",
                touchOk ? "ok" : "MISSING", imuOk ? "ok" : "MISSING", baro ? "present" : "absent");
  tofInit();
  xTaskCreatePinnedToCore(senseTask, "sense", 6144, nullptr, 17, nullptr, 0);
}
