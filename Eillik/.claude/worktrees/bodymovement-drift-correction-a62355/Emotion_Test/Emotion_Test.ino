// ============================================================
// Emotion_Test — integration test: face + touch + sound
//
// TAP the screen  -> random emotion: face animation + its sound.
//                    Faces with a WAV voice play the clip ~50% of
//                    the time, the synth chirp otherwise.
// IDLE 8 seconds  -> picks a new random emotion by itself.
// Boot            -> hello clip.
//
// Caption shows number + name; Serial (115200) logs each pick and
// whether the sound was a clip or synth.
//
// Wiring needed: display + touch + MAX98357A (no servos, no MPU).
// If the sketch doesn't fit in flash, set Tools -> Partition
// Scheme -> "Huge APP (3MB)".
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
unsigned long lastTap = 0, nextAuto = 0;

void randomEmotion() {
  int next;
  do { next = random(FACE_COUNT); } while (next == idx);  // always a new one
  idx = next;
  faceSet((FaceExpr)idx);
  soundPlayFace((FaceExpr)idx);   // clip or synth, always emotion-matched
  snprintf(caption, sizeof(caption), "%d/%d %s", idx + 1, FACE_COUNT, NAMES[idx]);
  faceLabel(caption);
  Serial.println(caption);
  nextAuto = millis() + 8000;
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

  faceSet(FACE_SMILE);
  faceLabel("1/38 SMILE  (tap me!)");
  soundPlayBoot();                // hello!
  nextAuto = millis() + 8000;
}

void loop() {
  faceUpdate();
  unsigned long now = millis();
  if (touch.available() && now - lastTap > 500) {
    lastTap = now;
    randomEmotion();
  } else if (now > nextAuto) {
    randomEmotion();              // entertains itself while you watch
  }
}
