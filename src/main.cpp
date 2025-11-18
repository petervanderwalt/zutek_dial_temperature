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

// --- Enums for State Management
enum ScreenState {
    MAIN_SCREEN,
    USER_MENU,
    SET_TEMP,
    SET_TIME,
    LOG_GRAPH,
    CONFIRM_START_TEST,
    SERVICE_MENU_LOGIN,
    SERVICE_MENU
};
ScreenState currentScreen = MAIN_SCREEN;

// --- Colors
unsigned short zutek_red = tft.color565(160, 0, 0);
unsigned short grays[15];

// --- Menu Variables
int userMenuSelection = 0;
String userMenuItems[] = {"Set Temperature", "Set Time", "Logging", "Pre-Heat", "Run Test", "Service Menu", "Back"};
const int userMenuSize = sizeof(userMenuItems) / sizeof(userMenuItems[0]);

int serviceMenuSelection = 0;
String serviceMenuItems[] = {"Calibrate Temp", "Set PID", "Diagnostics", "Back"};
const int serviceMenuSize = sizeof(serviceMenuItems) / sizeof(serviceMenuItems[0]);

int confirmMenuSelection = 0;

// --- Password State --- MODIFIED ---
const String correctPassword = "ABCDEF"; // New 6-character password
String enteredPassword = "";
int passwordCharIndex = 0;
bool showPasswordFail = false;
unsigned long passwordFailTime = 0;
const char* charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
const int charsetSize = 36;


// --- Values & State Flags
float currentTemp = 25.0;
float setpoint = 100.0;
int timeSettingMinutes = 180;

bool isTestRunning = false;
bool isPreheating = false;
bool startTestAfterPreheat = false;
unsigned long testStartTime = 0;

// --- Logging Mock Data
const int logDataPoints = 200;
float logData[logDataPoints];

#define EEPROM_SIZE 8
const int SETPOINT_ADDR = 0;
const int TIME_ADDR = 4;
long oldPosition = 0;

// Forward declarations
void drawMainScreen();
void drawRotaryMenu(const char *title, String items[], int numItems, int selection);
void drawPasswordScreen();
void drawMessageScreen(const char* msg1, const char* msg2, uint16_t color);
void drawConfirmationScreen(const char* title, const char* option1, const char* option2, int selection);
void drawValueEditor(const char *title, float value, const char *unit);
void drawTimeEditor();
void drawLogGraph();
void saveSettings();
void loadSettings();

// =========================================================
//               DRAWING FUNCTIONS
// =========================================================
void draw() {
    switch (currentScreen) {
        case MAIN_SCREEN: drawMainScreen(); break;
        case USER_MENU: drawRotaryMenu("User Menu", userMenuItems, userMenuSize, userMenuSelection); break;
        case SERVICE_MENU: drawRotaryMenu("Service Menu", serviceMenuItems, serviceMenuSize, serviceMenuSelection); break;
        case SERVICE_MENU_LOGIN: drawPasswordScreen(); break;
        case CONFIRM_START_TEST: drawConfirmationScreen("Start Test?", "Yes", "No", confirmMenuSelection); break;
        case SET_TEMP: drawValueEditor("Set Temperature", setpoint, "C"); break;
        case SET_TIME: drawTimeEditor(); break;
        case LOG_GRAPH: drawLogGraph(); break;
        default:
            spr.fillSprite(TFT_BLACK);
            spr.setTextDatum(MC_DATUM);
            spr.drawString("Screen Not Implemented", 120, 120);
            M5Dial.Display.pushImage(0, 0, 240, 240, (uint16_t *)spr.getPointer());
            break;
    }
}

void drawMainScreen() { /* Unchanged */
    spr.fillSprite(TFT_BLACK);
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.loadFont(Noto);
    char setpointBuf[32];
    sprintf(setpointBuf, "Set: %.1f C", setpoint);
    spr.drawString(setpointBuf, 120, 20);
    spr.setTextDatum(MC_DATUM);
    if (isPreheating) {
        spr.setTextColor(TFT_ORANGE, TFT_BLACK);
    } else {
        spr.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    spr.loadFont(bigFont);
    char tempBuf[16];
    sprintf(tempBuf, "%.1f C", currentTemp);
    spr.drawString(tempBuf, 120, 80);
    char timeBuf[32];
    if (isTestRunning) {
        unsigned long elapsedMillis = millis() - testStartTime;
        long remainingMillis = (long)timeSettingMinutes * 60000 - elapsedMillis;
        if (remainingMillis < 0) remainingMillis = 0;
        int hours = remainingMillis / 3600000;
        int mins = (remainingMillis / 60000) % 60;
        int secs = (remainingMillis / 1000) % 60;
        sprintf(timeBuf, "%02d:%02d:%02d", hours, mins, secs);
        spr.setTextColor(TFT_GREEN, TFT_BLACK);
    } else {
        int hours = timeSettingMinutes / 60;
        int minutes = timeSettingMinutes % 60;
        sprintf(timeBuf, "%02d:%02d:00", hours, minutes);
        spr.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    spr.drawString(timeBuf, 120, 135);
    spr.unloadFont();
    spr.setTextDatum(BC_DATUM);
    spr.loadFont(Noto);
    char statusBuf[32];
    if (isTestRunning) {
        strcpy(statusBuf, "Status: Running");
        spr.setTextColor(TFT_GREEN, TFT_BLACK);
    } else if (isPreheating) {
        strcpy(statusBuf, "Status: Preheating...");
        spr.setTextColor(TFT_ORANGE, TFT_BLACK);
    } else {
        strcpy(statusBuf, "Status: Idle");
        spr.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    spr.drawString(statusBuf, 120, 200);
    spr.setTextColor(grays[8], TFT_BLACK);
    spr.drawString("Click to Open Menu", 120, 220);
    spr.unloadFont();
    M5Dial.Display.pushImage(0, 0, 240, 240, (uint16_t *)spr.getPointer());
}

void drawRotaryMenu(const char *title, String items[], int numItems, int selection) { /* Unchanged */
    spr.fillSprite(TFT_BLACK);
    spr.loadFont(Noto);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.setTextDatum(TC_DATUM);
    spr.drawString(title, 120, 20);
    int centerY = 120;
    int itemSpacing = 40;
    for (int i = -2; i <= 2; i++) {
        int itemIndex = selection + i;
        if (itemIndex >= 0 && itemIndex < numItems) {
            int yPos = centerY + (i * itemSpacing);
            if (i == 0) {
                spr.fillRoundRect(10, yPos - 18, 220, 36, 5, TFT_WHITE);
                spr.setTextColor(TFT_BLACK, TFT_WHITE);
            } else {
                if (abs(i) == 1) {
                    spr.setTextColor(grays[5], TFT_BLACK);
                } else {
                    spr.setTextColor(grays[9], TFT_BLACK);
                }
            }
            spr.setTextDatum(MC_DATUM);
            spr.drawString(items[itemIndex], 120, yPos);
        }
    }
    spr.unloadFont();
    M5Dial.Display.pushImage(0, 0, 240, 240, (uint16_t *)spr.getPointer());
}

void drawPasswordScreen() {
    spr.fillSprite(TFT_BLACK);
    spr.loadFont(Noto);
    spr.setTextDatum(MC_DATUM);

    int radius = 105;
    for (int i = 0; i < charsetSize; i++) {
        float angle = (float)i / charsetSize * 2.0 * PI - (PI / 2.0);
        int x = 120 + radius * cos(angle);
        int y = 120 + radius * sin(angle);

        if (i == passwordCharIndex) {
            spr.fillCircle(x, y, 15, TFT_WHITE);
            spr.setTextColor(TFT_BLACK, TFT_WHITE);
            spr.setTextSize(2);
            spr.drawString(String(charset[i]), x, y);
            spr.setTextSize(1);
        } else {
            spr.setTextColor(TFT_WHITE, TFT_BLACK);
            spr.drawString(String(charset[i]), x, y);
        }
    }

    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawString("Enter Password", 120, 70);

    // --- MODIFICATION: Draw 6 placeholder slots ---
    int numChars = 6;
    int blockHeight = 40;
    int totalWidth = 180; // Increased width for 6 chars
    int charSlotWidth = totalWidth / numChars;
    int startY = 90;
    int startX = 120 - (totalWidth / 2);

    spr.drawRect(startX, startY, totalWidth, blockHeight, TFT_WHITE);

    for (int i = 1; i < numChars; i++) {
        int lineX = startX + (i * charSlotWidth);
        spr.drawLine(lineX, startY, lineX, startY + blockHeight, TFT_WHITE);
    }

    spr.setTextDatum(MC_DATUM);
    for (int i = 0; i < enteredPassword.length(); i++) {
        int charX = startX + (i * charSlotWidth) + (charSlotWidth / 2);
        int charY = startY + (blockHeight / 2);
        spr.setTextColor(TFT_GREEN, TFT_BLACK);
        spr.drawString("X", charX, charY);
    }

    spr.unloadFont();
    M5Dial.Display.pushImage(0, 0, 240, 240, (uint16_t *)spr.getPointer());
}

void drawMessageScreen(const char* msg1, const char* msg2, uint16_t color) { /* Unchanged */
    spr.fillSprite(TFT_BLACK);
    spr.loadFont(Noto);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(color, TFT_BLACK);
    spr.drawString(msg1, 120, 110);
    spr.drawString(msg2, 120, 140);
    spr.unloadFont();
    M5Dial.Display.pushImage(0, 0, 240, 240, (uint16_t *)spr.getPointer());
}

void drawConfirmationScreen(const char* title, const char* option1, const char* option2, int selection) { /* Unchanged */
    spr.fillSprite(TFT_BLACK);
    spr.loadFont(Noto);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.setTextDatum(TC_DATUM);
    spr.drawString(title, 120, 60);
    if (selection == 0) {
        spr.fillRoundRect(30, 110, 80, 40, 5, TFT_WHITE);
        spr.setTextColor(TFT_BLACK, TFT_WHITE);
    } else {
        spr.drawRoundRect(30, 110, 80, 40, 5, TFT_WHITE);
        spr.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    spr.setTextDatum(MC_DATUM);
    spr.drawString(option1, 70, 130);
    if (selection == 1) {
        spr.fillRoundRect(130, 110, 80, 40, 5, TFT_WHITE);
        spr.setTextColor(TFT_BLACK, TFT_WHITE);
    } else {
        spr.drawRoundRect(130, 110, 80, 40, 5, TFT_WHITE);
        spr.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    spr.setTextDatum(MC_DATUM);
    spr.drawString(option2, 170, 130);
    spr.unloadFont();
    M5Dial.Display.pushImage(0, 0, 240, 240, (uint16_t *)spr.getPointer());
}

void drawValueEditor(const char *title, float value, const char *unit) { /* Unchanged */
    spr.fillSprite(TFT_BLACK);
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.loadFont(Noto);
    spr.drawString(title, 120, 40);
    spr.unloadFont();
    spr.setTextDatum(MC_DATUM);
    spr.loadFont(bigFont);
    char buf[20];
    sprintf(buf, "%.1f %s", value, unit);
    spr.drawString(buf, 120, 120);
    spr.unloadFont();
    spr.setTextDatum(BC_DATUM);
    spr.loadFont(Noto);
    spr.setTextColor(grays[5], TFT_BLACK);
    spr.drawString("Click to Save", 120, 210);
    spr.unloadFont();
    M5Dial.Display.pushImage(0, 0, 240, 240, (uint16_t *)spr.getPointer());
}

void drawTimeEditor() { /* Unchanged */
    spr.fillSprite(TFT_BLACK);
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.loadFont(Noto);
    spr.drawString("Set Time", 120, 40);
    spr.unloadFont();
    int hours = timeSettingMinutes / 60;
    int minutes = timeSettingMinutes % 60;
    spr.setTextDatum(MC_DATUM);
    spr.loadFont(bigFont);
    char buf[20];
    sprintf(buf, "%02d:%02d", hours, minutes);
    spr.drawString(buf, 120, 120);
    spr.unloadFont();
    spr.setTextDatum(BC_DATUM);
    spr.loadFont(Noto);
    spr.setTextColor(grays[5], TFT_BLACK);
    spr.drawString("Click to Save", 120, 210);
    spr.unloadFont();
    M5Dial.Display.pushImage(0, 0, 240, 240, (uint16_t *)spr.getPointer());
}

void drawLogGraph() { /* Unchanged */
    spr.fillSprite(TFT_BLACK);
    int pad = 20;
    float minVal = logData[0], maxVal = logData[0];
    for (int i = 1; i < logDataPoints; i++) {
        if (logData[i] < minVal) minVal = logData[i];
        if (logData[i] > maxVal) maxVal = logData[i];
    }
    spr.drawLine(pad, pad, pad, 240 - pad, TFT_WHITE);
    spr.drawLine(pad, 240 - pad, 240 - pad, 240 - pad, TFT_WHITE);
    for (int i = 0; i < logDataPoints - 1; i++) {
        float x1 = pad + map(i, 0, logDataPoints, 0, 240 - (2 * pad));
        float y1 = (240 - pad) - map(logData[i], minVal, maxVal, 0, 240 - (2 * pad));
        float x2 = pad + map(i + 1, 0, logDataPoints, 0, 240 - (2 * pad));
        float y2 = (240 - pad) - map(logData[i + 1], minVal, maxVal, 0, 240 - (2 * pad));
        spr.drawLine(x1, y1, x2, y2, TFT_GREEN);
    }
    M5Dial.Display.pushImage(0, 0, 240, 240, (uint16_t *)spr.getPointer());
}

// =========================================================
//               SETUP & LOOP
// =========================================================
void setup() {
    auto cfg = M5.config();
    M5Dial.begin(cfg, true, true);
    spr.createSprite(240, 240);
    int co = 225;
    for (int i = 0; i < 15; i++) { grays[i] = tft.color565(co, co, co); co -= 15; }
    for (int i = 0; i < logDataPoints; i++) { logData[i] = 50.0 * sin(2.0 * PI * i / 100.0) + 100.0; }
    loadSettings();
    draw();
    M5Dial.Speaker.setVolume(180);
}

void loop() {
    M5Dial.update();

    if (showPasswordFail) {
        if (millis() - passwordFailTime > 1000) {
            showPasswordFail = false; currentScreen = MAIN_SCREEN; draw();
        }
        delay(20); return;
    }

    if (isPreheating) {
        if (currentTemp < setpoint) { currentTemp += 0.25; }
        else {
            currentTemp = setpoint; isPreheating = false; M5Dial.Speaker.tone(4000, 200);
            if (startTestAfterPreheat) {
                startTestAfterPreheat = false; currentScreen = CONFIRM_START_TEST; confirmMenuSelection = 0; draw();
            }
        }
    }
    if (isTestRunning) {
        if (millis() - testStartTime >= (unsigned long)timeSettingMinutes * 60000) {
            isTestRunning = false; userMenuItems[4] = "Run Test"; M5Dial.Speaker.tone(5000, 500);
        }
    }

    if (currentScreen == MAIN_SCREEN) { draw(); }

    long newPosition = M5Dial.Encoder.read();
    bool encoderMoved = abs(newPosition - oldPosition) >= 4;
    int encoderDir = (newPosition > oldPosition) ? 1 : -1;

    switch (currentScreen) {
        case MAIN_SCREEN:
            if (M5Dial.BtnA.wasPressed()) {
                currentScreen = USER_MENU; userMenuSelection = 0; draw();
            }
            break;

        case USER_MENU:
            if (encoderMoved) {
                int newSelection = userMenuSelection + encoderDir;
                userMenuSelection = constrain(newSelection, 0, userMenuSize - 1);
                oldPosition = newPosition; draw();
            }
            if (M5Dial.BtnA.wasPressed()) {
                String selection = userMenuItems[userMenuSelection];
                if (selection == "Back") { currentScreen = MAIN_SCREEN; }
                else if (selection == "Set Temperature") { currentScreen = SET_TEMP; }
                else if (selection == "Set Time") { currentScreen = SET_TIME; }
                else if (selection == "Logging") { currentScreen = LOG_GRAPH; }
                else if (selection == "Pre-Heat") {
                    startTestAfterPreheat = false; isPreheating = true; currentScreen = MAIN_SCREEN;
                }
                else if (selection == "Service Menu") {
                    enteredPassword = ""; passwordCharIndex = 0; currentScreen = SERVICE_MENU_LOGIN;
                }
                else if (userMenuItems[userMenuSelection] == "Run Test" || userMenuItems[userMenuSelection] == "Stop Test") {
                    if (isTestRunning) {
                        isTestRunning = false; userMenuItems[4] = "Run Test"; currentScreen = MAIN_SCREEN;
                    } else {
                        if (currentTemp < setpoint) {
                            isPreheating = true; startTestAfterPreheat = true; currentScreen = MAIN_SCREEN;
                        } else {
                            currentScreen = CONFIRM_START_TEST; confirmMenuSelection = 0;
                        }
                    }
                }
                oldPosition = M5Dial.Encoder.read(); draw();
            }
            break;

        case SERVICE_MENU_LOGIN:
            if (encoderMoved) {
                passwordCharIndex = (passwordCharIndex + encoderDir + charsetSize) % charsetSize;
                oldPosition = newPosition; draw();
            }
            if (M5Dial.BtnA.wasPressed()) {
                enteredPassword += charset[passwordCharIndex];
                // --- MODIFICATION: Check for 6 characters ---
                if (enteredPassword.length() == 6) {
                    if (enteredPassword == correctPassword) {
                        currentScreen = SERVICE_MENU; serviceMenuSelection = 0;
                    } else {
                        showPasswordFail = true; passwordFailTime = millis();
                        drawMessageScreen("Password Incorrect", "", TFT_RED);
                        return;
                    }
                }
                draw();
            }
            break;

        case SERVICE_MENU:
            if (encoderMoved) {
                int newSelection = serviceMenuSelection + encoderDir;
                serviceMenuSelection = constrain(newSelection, 0, serviceMenuSize - 1);
                oldPosition = newPosition; draw();
            }
            if (M5Dial.BtnA.wasPressed()) {
                if (serviceMenuItems[serviceMenuSelection] == "Back") {
                    currentScreen = USER_MENU;
                }
                draw();
            }
            break;

        case CONFIRM_START_TEST:
            if (encoderMoved) {
                confirmMenuSelection = (confirmMenuSelection + encoderDir + 2) % 2;
                oldPosition = newPosition; draw();
            }
            if (M5Dial.BtnA.wasPressed()) {
                if (confirmMenuSelection == 0) {
                    isTestRunning = true; testStartTime = millis(); userMenuItems[4] = "Stop Test";
                }
                currentScreen = MAIN_SCREEN; draw();
            }
            break;

        case SET_TEMP:
            if (encoderMoved) {
                setpoint += 0.5 * encoderDir; setpoint = constrain(setpoint, 0, 999.9);
                oldPosition = newPosition; draw();
            }
            if (M5Dial.BtnA.wasPressed()) {
                saveSettings(); currentScreen = USER_MENU; draw();
            }
            break;

        case SET_TIME:
            if (encoderMoved) {
                timeSettingMinutes += 1 * encoderDir;
                timeSettingMinutes = constrain(timeSettingMinutes, 0, (23 * 60 + 59));
                oldPosition = newPosition; draw();
            }
            if (M5Dial.BtnA.wasPressed()) {
                saveSettings(); currentScreen = USER_MENU; draw();
            }
            break;

        case LOG_GRAPH:
             if (M5Dial.BtnA.wasPressed()) {
                currentScreen = USER_MENU; draw();
            }
            break;
    }
    delay(5);
}

// =========================================================
//               HELPER FUNCTIONS
// =========================================================
void saveSettings() {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.put(SETPOINT_ADDR, setpoint);
    EEPROM.put(TIME_ADDR, timeSettingMinutes);
    EEPROM.commit();
}

void loadSettings() {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(SETPOINT_ADDR, setpoint);
    EEPROM.get(TIME_ADDR, timeSettingMinutes);
    if (isnan(setpoint) || setpoint < 0 || setpoint > 999.9) setpoint = 120.0;
    if (timeSettingMinutes < 0 || timeSettingMinutes > (24*60)) timeSettingMinutes = 30;
}
