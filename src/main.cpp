#include "M5Dial.h"
#include <Wire.h>
#include "bigFont.h"
#include "Noto.h"
#include "smallFont.h"
#include "M5Unified.h"
#include "M5GFX.h"
#include <TFT_eSPI.h>
#include <EEPROM.h>

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

// --- I2C / SHARED DATA ---
#define I2C_ADDR_MAINBOARD 0x42

#pragma pack(push, 1)
struct ControllerData {
    float currentTemp;
    float setpoint;
    float output;
    float kp;
    float ki;
    float kd;
    uint8_t isRunning;   // CHANGED: bool -> uint8_t (0=Off, 1=On)
    uint8_t isLogging;   // CHANGED: bool -> uint8_t (0=Off, 1=On)
    uint8_t errorState;
    uint32_t testDuration;
};
#pragma pack(pop)

ControllerData data;
bool i2cConnected = false;
unsigned long lastSync = 0;

// --- STATE MANAGEMENT ---
enum ScreenState {
    MAIN_SCREEN, USER_MENU, SET_TEMP, SET_TIME, LOG_GRAPH,
    CONFIRM_START_TEST, SERVICE_MENU_LOGIN, SERVICE_MENU,
    PID_SELECT_MENU, SET_KP, SET_KI, SET_KD
};
ScreenState currentScreen = MAIN_SCREEN;

// --- MENU VARS ---
unsigned short grays[15];

int userMenuSelection = 0;
String userMenuItems[] = {"Set Temperature", "Set Time", "Logging", "Run Test", "Service Menu", "Back"};
const int userMenuSize = 6;

int serviceMenuSelection = 0;
String serviceMenuItems[] = {"Set PID", "Diagnostics", "Back"};
const int serviceMenuSize = 3;

int pidMenuSelection = 0;
String pidMenuItems[] = {"Set Kp", "Set Ki", "Set Kd", "Back"};
const int pidMenuSize = 4;

int confirmMenuSelection = 0;

// --- PASSWORD ---
const String correctPassword = "ABCDEF";
String enteredPassword = "";
int passwordCharIndex = 0;
bool showPasswordFail = false;
unsigned long passwordFailTime = 0;
const char* charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
const int charsetSize = 36;

// --- SETTINGS ---
int timeSettingMinutes = 30;
long oldPosition = 0;
#define EEPROM_SIZE 8
const int TIME_ADDR = 4;

// --- LOGGING VISUALS ---
const int logDataPoints = 200;
float logData[logDataPoints];

// Forward Declarations
void drawMainScreen();
void drawRotaryMenu(const char *title, String items[], int numItems, int selection);
void drawPasswordScreen();
void drawMessageScreen(const char* msg1, const char* msg2, uint16_t color);
void drawConfirmationScreen(const char* title, const char* option1, const char* option2, int selection);
void drawValueEditor(const char *title, float &value, const char *unit, float step, float maxVal);
void drawTimeEditor();
void drawLogGraph();
void saveLocalSettings();
void loadLocalSettings();
void syncWithController();
void sendToController();

// ================= SETUP & LOOP =================

void setup() {
    auto cfg = M5.config();
    M5Dial.begin(cfg, true, true);
    spr.createSprite(240, 240);

    // Init I2C (Master) - M5Dial Internal I2C is usually 13/14
    Wire.begin(13, 15);

    int co = 225;
    for (int i = 0; i < 15; i++) { grays[i] = tft.color565(co, co, co); co -= 15; }
    for (int i = 0; i < logDataPoints; i++) { logData[i] = 25.0; }

    loadLocalSettings();
    M5Dial.Speaker.setVolume(180);

    // Default Fallbacks
    data.setpoint = 100.0;
    data.kp = 10.0; data.ki = 0.5; data.kd = 2.0;

    drawMainScreen();
}

void loop() {
    M5Dial.update();
    unsigned long now = millis();

    // --- I2C SYNC ---
    if (now - lastSync > 200) {
        lastSync = now;
        syncWithController();

        // Update Graph Data
        static unsigned long lastGraph = 0;
        if (now - lastGraph > 1000) {
            lastGraph = now;
            for (int i = 0; i < logDataPoints - 1; i++) logData[i] = logData[i+1];
            logData[logDataPoints - 1] = data.currentTemp;

            // Auto Stop Timer Logic
            if (data.isRunning && data.testDuration > (timeSettingMinutes * 60)) {
                data.isRunning = false;
                sendToController();
                M5Dial.Speaker.tone(4000, 1000);
                currentScreen = MAIN_SCREEN;
            }
        }
    }

    if (showPasswordFail) {
        if (millis() - passwordFailTime > 1000) {
            showPasswordFail = false; currentScreen = MAIN_SCREEN;
            drawMainScreen();
        }
        delay(20); return;
    }

    // Redraw Main Screen frequently for live updates
    if (currentScreen == MAIN_SCREEN) drawMainScreen();

    long newPosition = M5Dial.Encoder.read();
    bool encoderMoved = abs(newPosition - oldPosition) >= 4;
    int encoderDir = (newPosition > oldPosition) ? 1 : -1;

    switch (currentScreen) {
        case MAIN_SCREEN:
            if (M5Dial.BtnA.wasPressed()) {
                currentScreen = USER_MENU; userMenuSelection = 0;
                userMenuItems[3] = data.isRunning ? "Stop Test" : "Start Test";
                drawRotaryMenu("User Menu", userMenuItems, userMenuSize, userMenuSelection);
            }
            break;

        case USER_MENU:
            if (encoderMoved) {
                userMenuSelection = constrain(userMenuSelection + encoderDir, 0, userMenuSize - 1);
                oldPosition = newPosition;
                drawRotaryMenu("User Menu", userMenuItems, userMenuSize, userMenuSelection);
            }
            if (M5Dial.BtnA.wasPressed()) {
                String selection = userMenuItems[userMenuSelection];
                if (selection == "Back") { currentScreen = MAIN_SCREEN; }
                else if (selection == "Set Temperature") { currentScreen = SET_TEMP; drawValueEditor("Set Temperature", data.setpoint, "C", 0.5, 250.0); }
                else if (selection == "Set Time") { currentScreen = SET_TIME; drawTimeEditor(); }
                else if (selection == "Logging") { currentScreen = LOG_GRAPH; drawLogGraph(); }
                else if (selection == "Service Menu") {
                    enteredPassword = ""; passwordCharIndex = 0; currentScreen = SERVICE_MENU_LOGIN; drawPasswordScreen();
                }
                else if (selection == "Run Test" || selection == "Stop Test") {
                    if (data.isRunning) {
                        data.isRunning = false;
                        currentScreen = MAIN_SCREEN;
                    } else {
                        currentScreen = CONFIRM_START_TEST;
                        confirmMenuSelection = 0;
                        drawConfirmationScreen("Start Test?", "Yes", "No", confirmMenuSelection);
                    }
                    sendToController();
                }
                oldPosition = M5Dial.Encoder.read();
            }
            break;

        case SERVICE_MENU_LOGIN:
            if (encoderMoved) {
                passwordCharIndex = (passwordCharIndex + encoderDir + charsetSize) % charsetSize;
                oldPosition = newPosition; drawPasswordScreen();
            }
            if (M5Dial.BtnA.wasPressed()) {
                enteredPassword += charset[passwordCharIndex];
                if (enteredPassword.length() == 6) {
                    if (enteredPassword == correctPassword) {
                        currentScreen = SERVICE_MENU; serviceMenuSelection = 0;
                        drawRotaryMenu("Service Menu", serviceMenuItems, serviceMenuSize, serviceMenuSelection);
                    } else {
                        showPasswordFail = true; passwordFailTime = millis();
                        drawMessageScreen("Password Incorrect", "", TFT_RED);
                        return;
                    }
                }
                drawPasswordScreen();
            }
            break;

        case SERVICE_MENU:
            if (encoderMoved) {
                serviceMenuSelection = constrain(serviceMenuSelection + encoderDir, 0, serviceMenuSize - 1);
                oldPosition = newPosition;
                drawRotaryMenu("Service Menu", serviceMenuItems, serviceMenuSize, serviceMenuSelection);
            }
            if (M5Dial.BtnA.wasPressed()) {
                String sel = serviceMenuItems[serviceMenuSelection];
                if (sel == "Back") { currentScreen = USER_MENU; drawRotaryMenu("User Menu", userMenuItems, userMenuSize, userMenuSelection); }
                else if (sel == "Set PID") {
                    currentScreen = PID_SELECT_MENU; pidMenuSelection = 0;
                    drawRotaryMenu("PID Config", pidMenuItems, pidMenuSize, pidMenuSelection);
                }
            }
            break;

        case PID_SELECT_MENU:
            if (encoderMoved) {
                pidMenuSelection = constrain(pidMenuSelection + encoderDir, 0, pidMenuSize - 1);
                oldPosition = newPosition;
                drawRotaryMenu("PID Config", pidMenuItems, pidMenuSize, pidMenuSelection);
            }
            if (M5Dial.BtnA.wasPressed()) {
                String sel = pidMenuItems[pidMenuSelection];
                if (sel == "Back") { currentScreen = SERVICE_MENU; drawRotaryMenu("Service Menu", serviceMenuItems, serviceMenuSize, serviceMenuSelection); }
                else if (sel == "Set Kp") { currentScreen = SET_KP; drawValueEditor("Set Kp", data.kp, "", 0.1, 200.0); }
                else if (sel == "Set Ki") { currentScreen = SET_KI; drawValueEditor("Set Ki", data.ki, "", 0.01, 200.0); }
                else if (sel == "Set Kd") { currentScreen = SET_KD; drawValueEditor("Set Kd", data.kd, "", 0.1, 200.0); }
            }
            break;

       case CONFIRM_START_TEST:
            if (encoderMoved) {
                confirmMenuSelection = (confirmMenuSelection + encoderDir + 2) % 2;
                oldPosition = newPosition;
                drawConfirmationScreen("Start Test?", "Yes", "No", confirmMenuSelection);
            }
            if (M5Dial.BtnA.wasPressed()) {
                if (confirmMenuSelection == 0) { // YES selected
                    data.isRunning = 1; // Explicitly set to 1
                    sendToController();
                }
                currentScreen = MAIN_SCREEN;
            }
            break;

        case SET_TEMP:
        case SET_KP:
        case SET_KI:
        case SET_KD:
            if (encoderMoved) {
                float step = 0.5; float maxV = 250.0;
                if(currentScreen == SET_KP || currentScreen == SET_KD) step = 0.1;
                if(currentScreen == SET_KI) step = 0.01;

                float *target;
                const char* title;
                if(currentScreen == SET_TEMP) { target = &data.setpoint; title = "Set Temperature"; }
                else if(currentScreen == SET_KP) { target = &data.kp; title = "Set Kp"; }
                else if(currentScreen == SET_KI) { target = &data.ki; title = "Set Ki"; }
                else { target = &data.kd; title = "Set Kd"; }

                *target += (step * encoderDir);
                if (*target < 0) *target = 0;
                if (*target > maxV) *target = maxV;

                oldPosition = newPosition;
                drawValueEditor(title, *target, (currentScreen==SET_TEMP?"C":""), step, maxV);
            }
            if (M5Dial.BtnA.wasPressed()) {
                sendToController();
                if(currentScreen == SET_TEMP) { currentScreen = USER_MENU; drawRotaryMenu("User Menu", userMenuItems, userMenuSize, userMenuSelection); }
                else { currentScreen = PID_SELECT_MENU; drawRotaryMenu("PID Config", pidMenuItems, pidMenuSize, pidMenuSelection); }
            }
            break;

        case SET_TIME:
            if (encoderMoved) {
                timeSettingMinutes += 1 * encoderDir;
                timeSettingMinutes = constrain(timeSettingMinutes, 0, (23 * 60 + 59));
                oldPosition = newPosition; drawTimeEditor();
            }
            if (M5Dial.BtnA.wasPressed()) {
                saveLocalSettings(); currentScreen = USER_MENU; drawRotaryMenu("User Menu", userMenuItems, userMenuSize, userMenuSelection);
            }
            break;

        case LOG_GRAPH:
             if (M5Dial.BtnA.wasPressed()) {
                currentScreen = USER_MENU; drawRotaryMenu("User Menu", userMenuItems, userMenuSize, userMenuSelection);
            }
            break;
    }
    delay(5);
}

// ================= DRAWING =================
// (Implementations below match your provided styles)

void drawMainScreen() {
    spr.fillSprite(TFT_BLACK);
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.loadFont(Noto);

    if (!i2cConnected) {
        spr.setTextColor(TFT_RED, TFT_BLACK);
        spr.drawString("NO CONNECT", 120, 5);
        spr.setTextColor(TFT_WHITE, TFT_BLACK);
    } else {
        char setpointBuf[32];
        sprintf(setpointBuf, "Set: %.1f C", data.setpoint);
        spr.drawString(setpointBuf, 120, 20);
    }

    spr.setTextDatum(MC_DATUM);
    if (data.isRunning) {
        if (abs(data.currentTemp - data.setpoint) > 2.0) spr.setTextColor(TFT_ORANGE, TFT_BLACK);
        else spr.setTextColor(TFT_GREEN, TFT_BLACK);
    } else spr.setTextColor(TFT_WHITE, TFT_BLACK);

    spr.loadFont(bigFont);
    char tempBuf[16];
    sprintf(tempBuf, "%.1f C", data.currentTemp);
    spr.drawString(tempBuf, 120, 80);

    char timeBuf[32];
    if (data.isRunning) {
        long totalSeconds = (long)timeSettingMinutes * 60;
        long remaining = totalSeconds - data.testDuration;
        if (remaining < 0) remaining = 0;
        int hours = remaining / 3600;
        int mins = (remaining / 60) % 60;
        int secs = remaining % 60;
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
    if (data.errorState != 0) {
        spr.setTextColor(TFT_RED, TFT_BLACK);
        if(data.errorState == 1) strcpy(statusBuf, "ERR: SENSOR");
        else if(data.errorState == 2) strcpy(statusBuf, "ERR: OVERTEMP");
        else if(data.errorState == 3) strcpy(statusBuf, "ERR: USB LOST");
    } else if (data.isRunning) {
        strcpy(statusBuf, "Status: Running");
        spr.setTextColor(TFT_GREEN, TFT_BLACK);
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

void drawRotaryMenu(const char *title, String items[], int numItems, int selection) {
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
                if (abs(i) == 1) spr.setTextColor(grays[5], TFT_BLACK);
                else spr.setTextColor(grays[9], TFT_BLACK);
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
    int numChars = 6;
    int blockHeight = 40; int totalWidth = 180;
    int charSlotWidth = totalWidth / numChars;
    int startY = 90; int startX = 120 - (totalWidth / 2);
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

void drawMessageScreen(const char* msg1, const char* msg2, uint16_t color) {
    spr.fillSprite(TFT_BLACK);
    spr.loadFont(Noto);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(color, TFT_BLACK);
    spr.drawString(msg1, 120, 110);
    spr.drawString(msg2, 120, 140);
    spr.unloadFont();
    M5Dial.Display.pushImage(0, 0, 240, 240, (uint16_t *)spr.getPointer());
}

void drawConfirmationScreen(const char* title, const char* option1, const char* option2, int selection) {
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

void drawValueEditor(const char *title, float &value, const char *unit, float step, float maxVal) {
    spr.fillSprite(TFT_BLACK);
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.loadFont(Noto);
    spr.drawString(title, 120, 40);
    spr.unloadFont();
    spr.setTextDatum(MC_DATUM);
    spr.loadFont(bigFont);
    char buf[20];
    if (step < 0.1) sprintf(buf, "%.2f %s", value, unit);
    else sprintf(buf, "%.1f %s", value, unit);
    spr.drawString(buf, 120, 120);
    spr.unloadFont();
    spr.setTextDatum(BC_DATUM);
    spr.loadFont(Noto);
    spr.setTextColor(grays[5], TFT_BLACK);
    spr.drawString("Click to Save", 120, 210);
    spr.unloadFont();
    M5Dial.Display.pushImage(0, 0, 240, 240, (uint16_t *)spr.getPointer());
}

void drawTimeEditor() {
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

void drawLogGraph() {
    spr.fillSprite(TFT_BLACK);
    int pad = 20;
    float minVal = logData[0], maxVal = logData[0];
    for (int i = 1; i < logDataPoints; i++) {
        if (logData[i] < minVal) minVal = logData[i];
        if (logData[i] > maxVal) maxVal = logData[i];
    }
    if (abs(maxVal - minVal) < 0.1) { maxVal += 5; minVal -= 5; }

    spr.drawLine(pad, pad, pad, 240 - pad, TFT_WHITE);
    spr.drawLine(pad, 240 - pad, 240 - pad, 240 - pad, TFT_WHITE);

    for (int i = 0; i < logDataPoints - 1; i++) {
        float x1 = pad + map(i, 0, logDataPoints, 0, 240 - (2 * pad));
        float y1 = (240 - pad) - map(logData[i] * 10, minVal * 10, maxVal * 10, 0, 240 - (2 * pad));
        float x2 = pad + map(i + 1, 0, logDataPoints, 0, 240 - (2 * pad));
        float y2 = (240 - pad) - map(logData[i + 1] * 10, minVal * 10, maxVal * 10, 0, 240 - (2 * pad));
        spr.drawLine(x1, y1, x2, y2, TFT_GREEN);
    }
    float spY = (240 - pad) - map(data.setpoint * 10, minVal * 10, maxVal * 10, 0, 240 - (2 * pad));
    if(spY > pad && spY < (240-pad)) spr.drawFastHLine(pad, spY, 240-(2*pad), TFT_RED);

    M5Dial.Display.pushImage(0, 0, 240, 240, (uint16_t *)spr.getPointer());
}

void saveLocalSettings() {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.put(TIME_ADDR, timeSettingMinutes);
    EEPROM.commit();
}

void loadLocalSettings() {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(TIME_ADDR, timeSettingMinutes);
    if (timeSettingMinutes < 0 || timeSettingMinutes > (24*60)) timeSettingMinutes = 30;
}

// ================= I2C =================

void syncWithController() {
    uint8_t received = Wire.requestFrom(I2C_ADDR_MAINBOARD, sizeof(ControllerData));
    if (received == sizeof(ControllerData)) {
        i2cConnected = true;
        ControllerData incoming;
        Wire.readBytes((uint8_t*)&incoming, sizeof(ControllerData));

        data.currentTemp = incoming.currentTemp;
        data.output = incoming.output;
        data.errorState = incoming.errorState;
        data.testDuration = incoming.testDuration;
        data.isLogging = incoming.isLogging;

        // Only overwrite editable fields if NOT currently editing
        if (currentScreen != SET_TEMP && currentScreen != SET_KP &&
            currentScreen != SET_KI && currentScreen != SET_KD) {
            data.setpoint = incoming.setpoint;
            data.kp = incoming.kp;
            data.ki = incoming.ki;
            data.kd = incoming.kd;
            data.isRunning = incoming.isRunning;
        }
    } else {
        i2cConnected = false;
    }
}

void sendToController() {
    Wire.beginTransmission(I2C_ADDR_MAINBOARD);
    Wire.write((uint8_t*)&data, sizeof(ControllerData));
    Wire.endTransmission();
}
