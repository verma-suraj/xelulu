#include <Arduino.h>
#include <TFT_eSPI.h>       // The TFT library
#include <CST816S.h>        // Touch screen driver

// --- Hardware Pin Definitions ---
#define TOUCH_SDA 21
#define TOUCH_SCL 22
#define TOUCH_RST 33
#define TOUCH_INT 27

// Initialize the Touch Screen
CST816S touch(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_INT);

// Initialize the Display
TFT_eSPI tft = TFT_eSPI();

// Array of basic colors to cycle through
uint32_t colors[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_YELLOW, TFT_MAGENTA, TFT_CYAN, TFT_WHITE};
const int numColors = 7;
int colorIndex = 0;

// Debounce variables
unsigned long lastTouchTime = 0;
const int touchCooldown = 600; // Ignore touches for 600ms after a tap

void setup() {
  Serial.begin(115200);
  Serial.println("Starting LCD and Touch Test...");

  touch.begin();
  
  tft.init();
  tft.setRotation(0); 
  tft.fillScreen(colors[colorIndex]);
}

void loop() {
  // Check if a touch is detected
  if (touch.available()) {
    
    // Only process the touch if the cooldown period has passed
    if (millis() - lastTouchTime > touchCooldown) {
      
      // Update the timer for the next tap
      lastTouchTime = millis();
      
      // Move to the next color
      colorIndex++;
      if (colorIndex >= numColors) {
        colorIndex = 0; // Loop back to the start
      }

      Serial.print("Screen touched! Changing color index to: ");
      Serial.println(colorIndex);

      // Fill the screen with the new color
      tft.fillScreen(colors[colorIndex]);
    }
  }
}