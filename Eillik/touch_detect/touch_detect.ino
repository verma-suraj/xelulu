#define TOUCH_PIN T6

uint16_t baseline;

void setup() {
  Serial.begin(115200);

  uint32_t sum = 0;

  for (int i = 0; i < 100; i++) {
    sum += touchRead(TOUCH_PIN);
    delay(10);
  }

  baseline = sum / 100;
}

void loop() {
  bool touched = touchRead(TOUCH_PIN) < (baseline - 300);

  if (touched) {
    Serial.println("TOUCH");
  } else {
    Serial.println("NO TOUCH");
  }

  delay(20);
}