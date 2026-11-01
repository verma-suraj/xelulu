#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

// ESP32-S3 specific SPI and Control Pins[cite: 1, 24]
#define TFT_CS    10
#define TFT_DC    14
#define TFT_RST   21
#define TFT_MOSI  11  // Connected to display SDA
#define TFT_SCLK  12  // Connected to display SCL
#define TFT_EN    45  // Backlight Enable

// Utilizing Hardware SPI for optimal refresh rates
Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, TFT_CS, TFT_DC, TFT_RST);

void setup() {
  Serial.begin(115200);

  // 1. Power Stabilization Delay
  // Allows the 5V and 3.3V rails to completely plateau before acting
  delay(500);

  // 2. Sharp Backlight Trigger
  // Mimics the manual hot-plug by sending an instant 3.3V digital step
  pinMode(TFT_EN, OUTPUT);
  digitalWrite(TFT_EN, HIGH);

  // 3. Hardware SPI Routing
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS); 

  // 4. Manual Hardware Reset
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, HIGH);
  delay(10);
  digitalWrite(TFT_RST, LOW); 
  delay(15);
  digitalWrite(TFT_RST, HIGH);
  delay(150); // Allow ST7789 internal oscillator to stabilize

  // 5. Initialize the 2.0-inch driver at 240x320 resolution
  tft.init(240, 320); 
  tft.setRotation(1);

  // 6. Test Output
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(10, 20);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.println("Display Booted");
}

void loop() {
  // Main program logic
}