#include <Arduino.h>
#include <TFT_eSPI.h>       
#include <CST816S.h>        
#include "Face.h"           

// --- Hardware Pin Definitions ---
#define TOUCH_SDA 21
#define TOUCH_SCL 22
#define TOUCH_RST 33
#define TOUCH_INT 27

// Initialize the Touch Screen
CST816S touch(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_INT);

// Initialize the Display and Sprite Buffer
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite buffer = TFT_eSprite(&tft);

// Initialize Face: passing the sprite buffer, width (240), height (240), and eye size (60)
Face face(buffer, 240, 240, 60); 

const int NUM_EMOTIONS = 7;
int currentEmotionIndex = 0;

// Debounce variables
unsigned long lastTouchTime = 0;
const int touchCooldown = 600; 

// Helper function to switch emotions
void setEmotion(int index) {
  switch (index) {
    case 0: face.Expression.GoTo_Normal(); break;
    case 1: face.Expression.GoTo_Happy(); break;
    case 2: face.Expression.GoTo_Sad(); break;
    case 3: face.Expression.GoTo_Angry(); break;
    case 4: face.Expression.GoTo_Sleepy(); break;
    case 5: face.Expression.GoTo_Surprised(); break;
    case 6: face.Expression.GoTo_Focused(); break;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting Robot Eyes...");

  // 1. Start the I2C Touch Screen
  touch.begin();

  // 2. Start the Display
  tft.init();
  tft.setRotation(0); 
  tft.fillScreen(TFT_BLACK);

  // 3. Configure the Sprite Buffer in RAM
  buffer.setColorDepth(8);     
  
  // Create the canvas and check if it succeeded
  void* spritePtr = buffer.createSprite(240, 240); 
  
  if (spritePtr == nullptr) {
    Serial.println("CRITICAL ERROR: ESP32 still doesn't have enough RAM!");
  } else {
    Serial.println("SUCCESS: Sprite Buffer created in 8-bit mode!");
  }
  
  // ==========================================
  // THE FIX: Turn off the emotion autopilot 
  // so the touch screen has full control!
  // ==========================================
  face.RandomBehavior = false;
  
  // 4. Set the initial expression
  setEmotion(currentEmotionIndex);
}

void loop() {
  // 1. Update the face
  face.Update();

  // 2. Push the completed buffer to the screen
  buffer.pushSprite(0, 0);

  // 3. Listen for a touch on the screen (with Debounce)
  if (touch.available()) {
    if (millis() - lastTouchTime > touchCooldown) {
      lastTouchTime = millis();
      
      currentEmotionIndex++;
      if (currentEmotionIndex >= NUM_EMOTIONS) {
        currentEmotionIndex = 0; 
      }

      Serial.print("Screen touched! Emotion index: ");
      Serial.println(currentEmotionIndex);

      setEmotion(currentEmotionIndex);
    }
  }
}