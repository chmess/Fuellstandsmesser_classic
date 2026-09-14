#pragma once
#include <Arduino.h>

// AHT10 + VL53L0X/VL53L1X sensor module.
// Public API kept compatible with the former 20_Sensors.ino.

bool aht10Present();
float calculateDewPointMagnus(float temperatureC, float humidityPercent);
const __FlashStringHelper* ahtStatusText();
bool initAht10();
bool readAht10();

bool initL0X();
bool initL1X();
bool initToF();
bool readToF(uint16_t& mm);
