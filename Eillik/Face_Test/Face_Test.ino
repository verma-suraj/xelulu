// ============================================================
// Face_Test — expression verification sketch for Eilik_Bot
// 38 expressions from "Animation sheet.png" / Animations.md
//
// Tap the RIGHT side of the screen -> next face
// Tap the LEFT  side of the screen -> previous face
// Name + number shown at the bottom; every scripted animation
// runs live (breathing, blinks, tears, spirals, hearts, steam,
// flames, Zzz, eye rolls, darting, blush...).
//
// Only the display + touch need to be wired; servos/amp/MPU
// can stay disconnected.
//
// NOTE: EilikFace.h/.cpp and Pins.h here are COPIES of the ones
// in Eilik_Bot/. If you tweak face geometry during verification,
// copy the file back:
//   copy Face_Test\EilikFace.cpp Eilik_Bot\EilikFace.cpp
// ============================================================
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <CST816S.h>
#include "Pins.h"
#include "EilikFace.h"

CST816S touch(PIN_I2C_SDA, PIN_I2C_SCL, PIN_CTP_RST, PIN_CTP_INT);
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite buffer = TFT_eSprite(&tft);

// must match the FaceExpr enum order in EilikFace.h (sheet order)
const char* NAMES[FACE_COUNT] = {
  // Row 1: Joy & Excitement
  "SMILE", "GIDDY", "ECSTATIC", "CHARMED", "SPARKLING JOY",
  "PLAYFUL WINK", "STARRY EYES", "CHEERFUL LAUGH", "EAGER", "ANTICIPATION",
  // Row 2: Sadness & Anger
  "POUT", "TEARY EYES", "CRYING SOFTLY", "DISAPPOINTED", "HEARTBROKEN",
  "GRUMPY", "FURIOUS", "FUMING", "ANNOYED", "FLUSHED",
  // Row 3: Curiosity, Playful & Affection
  "QUESTIONING", "INVESTIGATING", "THOUGHTFUL", "CONFUSED SPIRAL", "WONDER",
  "PLAYFUL", "MISCHIEVOUS", "CHEEKY TONGUE", "SILLY DERP", "LOVING",
  // Row 4: Fatigue & Reactive
  "TIRED", "SLEEPING ZZZ", "YAWNING", "SLEEPY", "SCARED",
  "BORED MEH", "EMBARRASSED", "HUNGRY"
};

int idx = 0;
char caption[36];
unsigned long lastTap = 0;

void showFace() {
  faceSet((FaceExpr)idx);
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
  showFace();
}

void loop() {
  faceUpdate();
  if (touch.available() && millis() - lastTap > 400) {
    lastTap = millis();
    if (touch.data.x < 80) idx = (idx + FACE_COUNT - 1) % FACE_COUNT;  // left = back
    else                   idx = (idx + 1) % FACE_COUNT;               // right = next
    showFace();
  }
}
