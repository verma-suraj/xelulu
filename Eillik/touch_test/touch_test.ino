/*
  ESP32 Touch Test
  Touch Pad T6 = GPIO14
*/

#include <Arduino.h>

#define TOUCH_PIN T6   // GPIO14

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("================================");
  Serial.println("ESP32 Touch Test - GPIO14 (T6)");
  Serial.println("Touch the copper tape directly.");
  Serial.println("================================");
}

void loop() {
  uint16_t value = touchRead(TOUCH_PIN);

  Serial.print("Touch Value = ");
  Serial.println(value);

  delay(100);
}