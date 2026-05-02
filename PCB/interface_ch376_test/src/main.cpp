#include <Arduino.h>
#include <SPI.h>
#include <Ch376msc.h>

// --- PIN DEFINITIONS (ESP32 Standard) ---
#define PIN_USB_CS      4   // User defined
#define PIN_USB_INT     14  // User defined

// Standard ESP32 SPI Pins
// (Change these if your ESP32 board uses custom SPI mapping)
#define PIN_SPI_SCK     18
#define PIN_SPI_MISO    19
#define PIN_SPI_MOSI    23

// --- OBJECTS ---
// Initialize CH376: CS, INT, Speed (125kHz for safety during test)
Ch376msc flashDrive(PIN_USB_CS, PIN_USB_INT, SPI_SCK_KHZ(125));

// Helper: List files on USB
void listUSB() {
    Serial.println(F("--- Listing Root Files ---"));
    flashDrive.resetFileList();
    while(flashDrive.listDir()){
        Serial.print(F("FILE: "));
        Serial.println(flashDrive.getFileName());
    }
    Serial.println(F("--- End of List ---"));
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n\n--- ESP32 Standard CH376 Test ---");

    // 1. Initialize SPI explicitly
    // This ensures we are using the pins we think we are using
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);
    Serial.printf("[SPI] Initialized: SCK=%d, MISO=%d, MOSI=%d\n",
                  PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);

    // 2. Initialize CH376
    Serial.print("[CH376] Initializing... ");
    flashDrive.init();

    // 3. Hardware Ping Test
    if(flashDrive.pingDevice()){
        Serial.println("SUCCESS: Chip Communication OK!");
    } else {
        Serial.println("FAILED: Chip not responding (Check wiring/Power)");
        // Stop here if hardware fails
        while(1) { delay(1000); Serial.print("."); }
    }
}

void loop() {
    // Required: Handle CH376 Interrupts
    flashDrive.checkIntMessage();

    static unsigned long lastCheck = 0;
    static bool driveWasReady = false;

    // Check status every 1 second
    if (millis() - lastCheck > 1000) {
        lastCheck = millis();

        if (flashDrive.driveReady()) {
            if (!driveWasReady) {
                Serial.println("[USB] Drive Inserted - Mounting...");
                driveWasReady = true;
                listUSB();
            }
        } else {
            if (driveWasReady) {
                Serial.println("[USB] Drive Removed");
                driveWasReady = false;
            }
            // Optional: Keep pinging to prove chip is still alive
            if(!flashDrive.pingDevice()){
                Serial.println("[Error] Chip lost communication!");
            }
        }
    }
}
