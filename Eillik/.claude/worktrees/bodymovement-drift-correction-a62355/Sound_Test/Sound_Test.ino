// ============================================================
// Sound_Test — verify each emotion's sound against its animation
//
// Every face plays its matched sound the moment it appears, with
// the animation running — so you can judge whether the sound fits
// the emotion and its motion.
//
//   Tap RIGHT third of screen  -> next face (plays its sound)
//   Tap LEFT third of screen   -> previous face (plays its sound)
//   Tap MIDDLE of screen       -> replay current sound
//
// Wiring needed: display + touch + MAX98357A/speaker.
// Servos and MPU can stay disconnected.
//
// The audio task runs on core 0 while the face animates on core 1 —
// if the animation never stutters while sound plays, the non-blocking
// requirement from Eilik_Sound_Generation_Prompt.md is proven.
//
// NOTE: EilikFace.*, EilikSound.* and Pins.h here are COPIES from
// Eilik_Bot/. If you tune a sound here, copy the file back:
//   copy Sound_Test\EilikSound.cpp Eilik_Bot\EilikSound.cpp
// ============================================================
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <CST816S.h>
#include "Pins.h"
#include "EilikFace.h"
#include "EilikSound.h"

CST816S touch(PIN_I2C_SDA, PIN_I2C_SCL, PIN_CTP_RST, PIN_CTP_INT);
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite buffer = TFT_eSprite(&tft);

// must match the FaceExpr enum order in EilikFace.h (sheet order)
const char* NAMES[FACE_COUNT] = {
  "SMILE", "GIDDY", "ECSTATIC", "CHARMED", "SPARKLING JOY",
  "PLAYFUL WINK", "STARRY EYES", "CHEERFUL LAUGH", "EAGER", "ANTICIPATION",
  "POUT", "TEARY EYES", "CRYING SOFTLY", "DISAPPOINTED", "HEARTBROKEN",
  "GRUMPY", "FURIOUS", "FUMING", "ANNOYED", "FLUSHED",
  "QUESTIONING", "INVESTIGATING", "THOUGHTFUL", "CONFUSED SPIRAL", "WONDER",
  "PLAYFUL", "MISCHIEVOUS", "CHEEKY TONGUE", "SILLY DERP", "LOVING",
  "TIRED", "SLEEPING ZZZ", "YAWNING", "SLEEPY", "SCARED",
  "BORED MEH", "EMBARRASSED", "HUNGRY"
};

int idx = 0;
char caption[36];
unsigned long lastTap = 0;

void showFace(bool playSound) {
  faceSet((FaceExpr)idx);
  if (playSound) soundPlayFace((FaceExpr)idx);
  snprintf(caption, sizeof(caption), "%d/%d %s", idx + 1, FACE_COUNT, NAMES[idx]);
  faceLabel(caption);
  Serial.println(caption);
}

void setup() {
  Serial.begin(115200);
  touch.begin();
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  buffer.setColorDepth(8);
  buffer.createSprite(240, 240);
  faceBegin(&buffer);
  soundBegin();
  showFace(true);
}

void loop() {
  faceUpdate();   // must stay perfectly smooth even while sound plays
  if (touch.available() && millis() - lastTap > 400) {
    lastTap = millis();
    int x = touch.data.x;
    if (x < 80)       { idx = (idx + FACE_COUNT - 1) % FACE_COUNT; showFace(true); }
    else if (x > 160) { idx = (idx + 1) % FACE_COUNT; showFace(true); }
    else              soundPlayFace((FaceExpr)idx);   // middle = replay
  }
}
