#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <Arduino.h>

#define I2C_ADDR_MAINBOARD 0x42

// force byte-alignment so main S3 and Dial don't misinterpret data positions
#pragma pack(push, 1)
struct ControllerData {
    float currentTemp;
    float setpoint;
    float output;
    float kp;
    float ki;
    float kd;
    bool isRunning;
    bool isLogging;
    uint8_t errorState;
    uint32_t testDuration;
};
#pragma pack(pop)

#endif
