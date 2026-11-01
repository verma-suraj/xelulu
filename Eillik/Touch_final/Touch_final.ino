/*
==============================================================
ESP32 Touch Detection
Board : ESP32 Dev Module
Touch Pad : T6 (GPIO14)
==============================================================
*/

#include <Arduino.h>

#define TOUCH_PIN T9

// Thresholds based on your measurements
#define SINGLE_TOUCH_THRESHOLD 1325
#define STRONG_TOUCH_THRESHOLD 1050

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("ESP32 Touch Detection");
}

void loop() {

  uint16_t value = touchRead(TOUCH_PIN);

  Serial.print("Touch Value = ");
  Serial.print(value);

  if (value < STRONG_TOUCH_THRESHOLD) {
    Serial.print("    >>> STRONG TOUCH (3 Fingers)");
  }
  else if (value < SINGLE_TOUCH_THRESHOLD) {
    Serial.print("    >>> SINGLE FINGER");
  }

  Serial.println();

  delay(20);
}