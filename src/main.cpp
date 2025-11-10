#include "M5Dial.h"
#include "bigFont.h"
#include "Noto.h"
#include "smallFont.h"
#include "Wire.h"
#include "M5Unified.h"
#include "M5GFX.h"
#include <TFT_eSPI.h>
#include <EEPROM.h>

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

// colors

unsigned short zutek_blue = tft.color565(0, 0, 255);
unsigned short zutek_darkblue = tft.color565(0, 0, 80);
unsigned short zutek_red = tft.color565(160, 0, 0);


unsigned short grays[15];
unsigned short blue[2]   = {zutek_darkblue, zutek_blue };
unsigned short red       = zutek_red;
unsigned short green     = 0x1383;
unsigned short onOffCol[2] = {red, green};



bool mode = 0;
unsigned long lastInteractionTime = 0;
const unsigned long SET_MODE_TIMEOUT = 2000; // 2-second timeout

int deb = 0;

// values
float currentTemp = 45.37;
float setpoint = 120.5;

int aniFrame = 0;
int ani = 0;
int bri = 3;
double rad = 0.01745;


#define EEPROM_SIZE 4 // Size to store one float
const int SETPOINT_ADDR = 0; // EEPROM address for setpoint


long oldPosition = 0;

// =========================================================
void draw() {
  spr.fillSprite(TFT_BLACK);

  // top capsule
  spr.fillSmoothRoundRect(0, 8, 170, 42, 10, blue[mode]);
  spr.fillSmoothRoundRect(0, 18, 160, 22, 3, zutek_red, TFT_BLACK);
  spr.setTextDatum(0);
  spr.setTextColor(grays[1], zutek_red);
  spr.loadFont(Noto);
  spr.drawString("TEMPERATURE", 50, 23);
  spr.unloadFont();

  // main current temp (big) - Moved up
  spr.setTextDatum(4);
  if (mode) {
    spr.setTextColor(grays[6], TFT_BLACK);
  } else {
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
  }
  spr.loadFont(bigFont);
  char buf[16];
  sprintf(buf, "%.2f", currentTemp);
  spr.drawString(buf, 116, 88);
  spr.unloadFont();

  // progress bar region mock - Moved between temp and setpoint
  for (int i = 0; i < 24; i++)
    spr.drawWedgeLine(10 + (i * 12), 127, 20 + (i * 12), 112, 3, 3, grays[11]);
  float frac = constrain(currentTemp / setpoint, 0.0f, 1.0f);
  for (int i = 0; i < (int)(24 * frac); i++)
    spr.drawWedgeLine(10 + (i * 12), 127, 20 + (i * 12), 112, 3, 3, zutek_red);
  spr.fillRect(0, 108, 240, 6, TFT_BLACK);
  spr.fillRect(0, 127, 240, 6, TFT_BLACK);

  // setpoint small text at bottom capsule - Moved down
  spr.setTextDatum(4);
  if (mode) {
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
  } else {
    spr.setTextColor(grays[4], TFT_BLACK);
  }
  spr.loadFont(bigFont);
  char buf2[16];
  // setpoint
  sprintf(buf2, "%.2f", setpoint);
  spr.drawString(buf2, 116, 167);
  spr.unloadFont();

  // bottom capsule
  spr.fillSmoothRoundRect(95, 194, 124, 42, 10, blue[mode]);


  if (mode) {
    spr.fillSmoothRoundRect(105, 204, 120, 22, 3, zutek_red, TFT_BLACK);
    spr.setTextDatum(0);
    spr.setTextColor(grays[1], zutek_red);
    spr.loadFont(Noto);
    spr.drawString("SETPOINT", 110, 208);
    spr.unloadFont();
  }


  M5Dial.Display.pushImage(0, 0, 240, 240, (uint16_t *)spr.getPointer());
}

void saveSetpoint() {
  EEPROM.put(SETPOINT_ADDR, setpoint);
  EEPROM.commit();
}

// =========================================================
void setup() {
  auto cfg = M5.config();
  M5Dial.begin(cfg, true, true);

  spr.createSprite(240, 240);

  int co = 225;
  for (int i = 0; i < 15; i++) {
    grays[i] = tft.color565(co, co, co);
    co -= 15;
  }

  // --- Load setpoint from EEPROM
   EEPROM.begin(EEPROM_SIZE);
   EEPROM.get(SETPOINT_ADDR, setpoint);
   // If EEPROM is empty/corrupt, set a default value
   if (isnan(setpoint) || setpoint < 0 || setpoint > 999.99) {
     setpoint = 120.0;
   }

  draw();

  M5Dial.Speaker.setVolume(180);

}

// =========================================================
void loop() {
  aniFrame++;
  if (aniFrame > 2) {
    aniFrame = 0;
    ani++;
    if (ani > 8) ani = 0;
  }

  M5Dial.update();

  if (mode) {
   long newPosition = M5Dial.Encoder.read();
   if (abs(newPosition - oldPosition) >= 4) { // smooth encoder (÷4)
     M5Dial.Speaker.tone(6000, 100);  // frequency, duration
     if (newPosition > oldPosition) setpoint += 0.5;
     else setpoint -= 0.5; // Corrected decrement
     oldPosition = newPosition;

     // Constrain values
     if (setpoint < 0) setpoint = 0;
     if (setpoint > 999.99) setpoint = 999.99;

     lastInteractionTime = millis(); // Reset timeout timer
     draw();
   }
 }



  if (M5Dial.BtnA.wasPressed()) {
    mode = !mode;
    if (mode) {
      // ENTERING set mode
      M5Dial.Speaker.tone(5000, 400);  // frequency, duration
      lastInteractionTime = millis(); // Start the timeout timer
    } else {
      // EXITING set mode
      M5Dial.Speaker.tone(7000, 400);  // frequency, duration
      saveSetpoint();
    }
    draw();
  }

  delay(5);
}
