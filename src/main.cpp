#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_MAX31865.h>
#include <PID_v1.h>
#include <Ch376msc.h>
#include "SharedData.h"
#include "WebPage.h"

// --- CONFIGURATION ---
const char* ssid = "zutek_pid";
const char* password = "password123";

// --- PIN DEFINITIONS ---
#define PIN_NEOPIXEL    48
#define PIN_MOSFET      8
#define PIN_SPI_MOSI    35
#define PIN_SPI_SCK     36
#define PIN_SPI_MISO    37
#define PIN_CS_PT100    1
#define PIN_CS_SD       5
#define PIN_CS_USB      6
#define PIN_INT_USB     7
#define PIN_I2C_SDA     3
#define PIN_I2C_SCL     4

// --- OBJECTS ---
// UPDATED: Only 2 LEDs (0=Mainboard, 1=SSR/PT100 Board)
Adafruit_NeoPixel strip(2, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
Adafruit_MAX31865 thermo = Adafruit_MAX31865(PIN_CS_PT100);
Ch376msc flashDrive(PIN_CS_USB, PIN_INT_USB, SPI_SCK_KHZ(125));
AsyncWebServer server(80);

// --- PID VARIABLES ---
double pidInput, pidOutput, pidSetpoint;
double Kp = 10.0, Ki = 0.5, Kd = 2.0;
PID myPID(&pidInput, &pidOutput, &pidSetpoint, Kp, Ki, Kd, DIRECT);

// --- SYSTEM STATE ---
ControllerData sysData;
unsigned long lastLogTime = 0;
unsigned long lastSafetyCheck = 0;
String currentLogFileName = "";
int WindowSize = 1000;
unsigned long windowStartTime;

// --- SAFETY SETTINGS ---
#define MAX_TEMP_LIMIT 150.0
#define RUNAWAY_TIMEOUT_MS 60000
#define MIN_RISE_PER_MIN 1.0
float lastSafetyTemp = 0;

// --- HELPER FUNCTIONS ---

void setupWiFi() {
    WiFi.softAP(ssid, password);
    Serial.print("AP IP Address: ");
    Serial.println(WiFi.softAPIP());
}

void createLogFile() {
    if (!flashDrive.driveReady()) return;
    int fileIndex = 1;
    char filename[13];
    while(true) {
        sprintf(filename, "LOG_%03d.CSV", fileIndex);
        if (flashDrive.openFile() == 0) {
           flashDrive.closeFile();
           fileIndex++;
        } else {
            flashDrive.setFileName(filename);
            flashDrive.openFile();
            char header[] = "Time_s,Setpoint_C,Temp_C,Output_PWM\n";
            flashDrive.writeFile(header, strlen(header));
            flashDrive.closeFile();
            currentLogFileName = String(filename);
            Serial.printf("[USB] New Log Created: %s\n", filename);
            break;
        }
        if (fileIndex > 999) break;
    }
}

void logDataToUSB() {
    if (!sysData.isLogging || sysData.errorState == 3) return;

    if (flashDrive.driveReady()) {
        char buf[64];
        sprintf(buf, "%lu,%.2f,%.2f,%.0f\n",
            millis()/1000, sysData.setpoint, sysData.currentTemp, sysData.output);

        flashDrive.setFileName(currentLogFileName.c_str());
        if(flashDrive.openFile() == 0) { }
        flashDrive.moveCursor(0xFFFFFFFF);
        flashDrive.writeFile(buf, strlen(buf));
        flashDrive.closeFile();
    } else {
        sysData.errorState = 3; // USB Lost
    }
}

void onRequest() {
    Wire.write((uint8_t*)&sysData, sizeof(ControllerData));
}

void onReceive(int howMany) {
    if (howMany >= sizeof(ControllerData)) {
        ControllerData incoming;
        Wire.readBytes((uint8_t*)&incoming, sizeof(ControllerData));
        sysData.setpoint = incoming.setpoint;
        sysData.isRunning = incoming.isRunning;
        if (incoming.kp != sysData.kp || incoming.ki != sysData.ki || incoming.kd != sysData.kd) {
            sysData.kp = incoming.kp;
            sysData.ki = incoming.ki;
            sysData.kd = incoming.kd;
            myPID.SetTunings(sysData.kp, sysData.ki, sysData.kd);
        }
    }
}

// Helper: Safe Sensor Reading with Rate Limiting
void checkSensor(unsigned long now) {
    static unsigned long lastTempRead = 0;
    static unsigned long lastErrorLog = 0;

    if (now - lastTempRead > 250) {
        lastTempRead = now;
        uint8_t fault = thermo.readFault();

        if (fault) {
            sysData.errorState = 1; // 1 = Sensor Fault
            sysData.isRunning = false;
            sysData.output = 0;
            sysData.currentTemp = 0.0;
            thermo.clearFault();

            if (now - lastErrorLog > 2000) {
                lastErrorLog = now;
                if (fault == 0xFF) Serial.println("[ERR] PT100 Disconnected (0xFF)");
                else Serial.printf("[ERR] MAX31865 Fault Code: 0x%02X\n", fault);
            }
        } else {
            sysData.currentTemp = thermo.temperature(100, 430);
            if (sysData.errorState == 1) {
                sysData.errorState = 0;
                Serial.println("[INF] PT100 Sensor Restored");
            }
        }
    }
}

void setup() {
    Serial.begin(115200);

    sysData.setpoint = 100.0;
    sysData.kp = Kp; sysData.ki = Ki; sysData.kd = Kd;
    sysData.isRunning = false;
    sysData.errorState = 0;

    pinMode(PIN_MOSFET, OUTPUT);
    digitalWrite(PIN_MOSFET, LOW);
    strip.begin(); strip.show();

    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);
    Wire.begin(I2C_ADDR_MAINBOARD, PIN_I2C_SDA, PIN_I2C_SCL, 100000);
    Wire.onRequest(onRequest);
    Wire.onReceive(onReceive);

    thermo.begin(MAX31865_3WIRE);

    flashDrive.init();

    windowStartTime = millis();
    myPID.SetOutputLimits(0, 255);
    myPID.SetMode(AUTOMATIC);

    setupWiFi();

    // --- API ENDPOINTS ---
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
        JsonDocument doc;
        doc["temp"] = sysData.currentTemp;
        doc["sp"] = sysData.setpoint;
        doc["out"] = sysData.output;
        doc["run"] = sysData.isRunning;
        doc["log"] = sysData.isLogging;
        doc["err"] = sysData.errorState;
        doc["kp"] = sysData.kp; doc["ki"] = sysData.ki; doc["kd"] = sysData.kd;
        doc["time"] = millis()/1000;
        String res; serializeJson(doc, res);
        request->send(200, "application/json", res);
    });

    server.on("/api/set", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("sp")) sysData.setpoint = request->getParam("sp")->value().toFloat();
        if (request->hasParam("kp")) {
             sysData.kp = request->getParam("kp")->value().toFloat();
             sysData.ki = request->getParam("ki")->value().toFloat();
             sysData.kd = request->getParam("kd")->value().toFloat();
             myPID.SetTunings(sysData.kp, sysData.ki, sysData.kd);
        }
        request->send(200, "text/plain", "OK");
    });

    server.on("/api/toggle", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("run")) {
            bool run = request->getParam("run")->value().toInt() == 1;
            sysData.isRunning = run;
            if (run) {
                sysData.errorState = 0;
                createLogFile();
                sysData.isLogging = true;
                sysData.testDuration = 0;
            } else {
                sysData.isLogging = false;
            }
        }
        request->send(200, "text/plain", "OK");
    });

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", index_html);
        response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        response->addHeader("Pragma", "no-cache");
        response->addHeader("Expires", "0");
        request->send(response);
    });

    server.begin();
    Serial.println("System Ready");
}

void loop() {
    unsigned long now = millis();

    // 1. USB Maintenance
    flashDrive.checkIntMessage();

    // 2. Read Sensor Safely
    checkSensor(now);

    // 3. Safety Check
    if (sysData.isRunning && sysData.errorState == 0 && sysData.output > 200) {
        if (now - lastSafetyCheck > RUNAWAY_TIMEOUT_MS) {
            lastSafetyTemp = sysData.currentTemp;
            lastSafetyCheck = now;
        }
    } else {
        lastSafetyCheck = now;
        lastSafetyTemp = sysData.currentTemp;
    }

    if (sysData.currentTemp > MAX_TEMP_LIMIT) {
        sysData.errorState = 2; // Overheat
        sysData.isRunning = false;
        sysData.output = 0;
    }

    // 4. PID Loop
    if (sysData.isRunning && sysData.errorState == 0) {
        pidInput = sysData.currentTemp;
        pidSetpoint = sysData.setpoint;
        myPID.Compute();
        sysData.output = pidOutput;
    } else {
        sysData.output = 0;
    }

    // 5. SSR Control
    unsigned long timeInWindow = now - windowStartTime;
    if (timeInWindow > WindowSize) {
        windowStartTime += WindowSize;
        timeInWindow = 0;
    }
    double onDuration = 0;
    if (sysData.errorState == 0) onDuration = (sysData.output / 255.0) * WindowSize;
    if (onDuration > timeInWindow) digitalWrite(PIN_MOSFET, HIGH);
    else digitalWrite(PIN_MOSFET, LOW);

    // 6. Logging & LEDs
    if (now - lastLogTime > 1000) {
        lastLogTime = now;
        if (sysData.isRunning) {
            sysData.testDuration++;
            logDataToUSB();
        }

        // --- LED CONTROL ---

        // PIXEL 0: Mainboard (System Status / USB Error)
        if (sysData.errorState == 3) {
            // USB Error -> Blink Magenta
            if ((now / 500) % 2 == 0) strip.setPixelColor(0, strip.Color(255, 0, 255));
            else strip.setPixelColor(0, 0);
        } else {
            // Normal Operation -> Heartbeat Blue
            int b = (sin(now/500.0)+1)*60;
            strip.setPixelColor(0, strip.Color(0, 0, b));
        }

        // PIXEL 1: PT100/SSR Board (Heat / Sensor Error)
        if (sysData.errorState == 1) {
            // Sensor Fail -> Blink Orange
            if ((now / 500) % 2 == 0) strip.setPixelColor(1, strip.Color(255, 100, 0));
            else strip.setPixelColor(1, 0);
        }
        else if (sysData.errorState == 2) {
            // Overheat -> Solid Red
            strip.setPixelColor(1, strip.Color(255, 0, 0));
        }
        else {
            // Normal -> Show Heater Intensity (Red)
            int r = (int)sysData.output;
            strip.setPixelColor(1, strip.Color(r, 0, 0));
        }

        strip.show();
    }
}
