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

// Initialize Face: width (240), height (240), and eye size ENLARGED to 120!
Face face(buffer, 240, 240, 120); 

const int NUM_EMOTIONS = 6;
int currentEmotionIndex = 0;

// Debounce variables
unsigned long lastTouchTime = 0;
const int touchCooldown = 600; 

// =================================================================
// CUSTOM EILIK EXPRESSIONS (Fixed for ESP32_Faces Library)
// =================================================================
void setEilikEmotion(int index) {
  // First, reset the eyes back to normal
  face.Expression.ClearVariations();

  switch (index) {
    case 0: 
      // 1. Eilik Normal (Neutral pills)
      face.LeftEye.TransitionTo(Preset_Normal);
      face.RightEye.TransitionTo(Preset_Normal);
      // [SOUND EFFECT PLACEHOLDER: Play "Neutral/Idle" sound here]
      break;

    case 1: 
      // 2. Eilik Happy (Squashed height for a smiling squint)
      face.LeftEye.TransitionTo(Preset_Happy);
      face.RightEye.TransitionTo(Preset_Happy);
      face.LeftEye.Variation1.Values.Height = -20;
      face.RightEye.Variation1.Values.Height = -20;
      // [SOUND EFFECT PLACEHOLDER: Play "Happy/Giggle" sound here]
      break;

    case 2: 
      // 3. Eilik Angry (Sharp inward slants)
      face.LeftEye.TransitionTo(Preset_Angry);
      face.RightEye.TransitionTo(Preset_Angry);
      face.LeftEye.Variation1.Values.Slope_Top = -0.5; 
      face.RightEye.Variation1.Values.Slope_Top = 0.5;
      face.LeftEye.Variation1.Values.OffsetY = 5;
      face.RightEye.Variation1.Values.OffsetY = 5;
      // [SOUND EFFECT PLACEHOLDER: Play "Angry/Growl" sound here]
      break;

    case 3: 
      // 4. Eilik Sad (Drooping outer corners)
      face.LeftEye.TransitionTo(Preset_Sad);
      face.RightEye.TransitionTo(Preset_Sad);
      face.LeftEye.Variation1.Values.Slope_Top = 0.5;
      face.RightEye.Variation1.Values.Slope_Top = -0.5;
      // [SOUND EFFECT PLACEHOLDER: Play "Sad/Whimper" sound here]
      break;

    case 4: 
      // 5. Eilik Sleepy / Bored (Squinting, almost flat lines)
      face.LeftEye.TransitionTo(Preset_Sleepy);
      face.RightEye.TransitionTo(Preset_Sleepy);
      face.LeftEye.Variation1.Values.Height = -30;
      face.RightEye.Variation1.Values.Height = -30;
      // [SOUND EFFECT PLACEHOLDER: Play "Yawn/Sleep" sound here]
      break;

    case 5: 
      // 6. Eilik Shocked (Massive, wide-open circles)
      face.LeftEye.TransitionTo(Preset_Surprised);
      face.RightEye.TransitionTo(Preset_Surprised);
      face.LeftEye.Variation1.Values.Height = 15;
      face.RightEye.Variation1.Values.Height = 15;
      face.LeftEye.Variation1.Values.Width = 15;
      face.RightEye.Variation1.Values.Width = 15;
      // [SOUND EFFECT PLACEHOLDER: Play "Shocked/Gasp" sound here]
      break;
  }
}

void setup() {
  Serial.begin(115200);

  touch.begin();
  tft.init();
  tft.setRotation(0); 
  tft.fillScreen(TFT_BLACK);

  // Buffer in 8-bit mode to save RAM
  buffer.setColorDepth(8);     
  buffer.createSprite(240, 240); 
  
  // Enlarge the gap between the bigger eyes
  face.EyeInterDistance = 20; 

  // Turn off the autopilot so they don't change randomly
  face.RandomBehavior = false;
  
  // Set the initial Eilik expression
  setEilikEmotion(currentEmotionIndex);
}

void loop() {
  // Update and draw the face
  face.Update();
  buffer.pushSprite(0, 0);

  // Listen for touch
  if (touch.available()) {
    if (millis() - lastTouchTime > touchCooldown) {
      lastTouchTime = millis();
      
      currentEmotionIndex++;
      if (currentEmotionIndex >= NUM_EMOTIONS) {
        currentEmotionIndex = 0; 
      }

      Serial.print("Screen touched! Eilik emotion index: ");
      Serial.println(currentEmotionIndex);

      setEilikEmotion(currentEmotionIndex);
    }
  }
}