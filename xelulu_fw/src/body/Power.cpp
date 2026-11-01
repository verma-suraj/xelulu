#include "Power.h"
#include "../State.h"
#include "../store/Store.h"

static bool brownoutBoot = false;

void powerBoot(uint32_t approxEpoch) {
  S.batteryPct = 80;                      // unknown until the rail sags: report the "healthy" level
  if (esp_reset_reason() != ESP_RST_BROWNOUT) return;
  brownoutBoot = true;
  brownoutLog(approxEpoch);
  faultSet(F_BROWNOUT_RESET);
  if (brownoutTwiceWithin(600)) {         // two within 10 min: low-power until a healthy boot
    S.lowPower = true;
    faultSet(F_LOW_POWER_MODE);
    Serial.println("[power] 2 brownouts in 10 min: LOW-POWER mode (no wheels, half arm moves, -6 dB)");
  }
}

void powerTick(uint32_t now) {
  static uint8_t level = 80;
  static uint32_t below[3] = {0, 0, 0}, aboveSince = 0, healthySince = 0, logAt = 0;
  uint16_t mv = S.railMv;
  if (mv) {
    // falls after 30 s below a threshold; rises only after 5 min above it (or a reboot)
    const uint16_t thr[3] = {cal.railLowMv, cal.railCritMv, cal.railEmptyMv};
    const uint8_t lv[3] = {20, 10, 5};
    for (uint8_t i = 0; i < 3; i++) {
      if (mv < thr[i]) { if (!below[i]) below[i] = now; }
      else below[i] = 0;
      if (below[i] && now - below[i] >= 30000 && lv[i] < level) {
        level = lv[i];
        Serial.printf("[power] rail %u mV below %u mV for 30 s: estimate %u%%\n", mv, thr[i], level);
      }
    }
    uint16_t need = level == 80 ? 0 : level == 20 ? cal.railOkMv : level == 10 ? cal.railLowMv : cal.railCritMv;
    if (level < 80 && mv >= need) {
      if (!aboveSince) aboveSince = now;
      else if (now - aboveSince >= 300000) {
        level = level == 5 ? 10 : level == 10 ? 20 : 80;
        aboveSince = 0;
      }
    } else aboveSince = 0;
    if (mv < cal.railLowMv) faultSet(F_RAIL_LOW);
    else if (mv >= cal.railOkMv && faultOn(F_RAIL_LOW)) faultClear(F_RAIL_LOW);

    // a boot that is not a brownout, with a healthy rail for 30 s, ends the brownout story
    if (!brownoutBoot && mv >= cal.railOkMv) {
      if (!healthySince) healthySince = now;
      else if (now - healthySince > 30000) { brownoutClear(); healthySince = UINT32_MAX - 1; }
    }
  }
  S.batteryPct = level;
  if (now - logAt >= 60000) {
    logAt = now;
    Serial.printf("[power] rail %u mV  est %u  brownouts %s%s\n", mv, level,
                  faultOn(F_BROWNOUT_RESET) ? "this boot" : "0", S.lowPower ? "  LOW-POWER" : "");
  }
}
