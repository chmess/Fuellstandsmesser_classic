#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "AppTypes.h"
#include "AppConstants.h"

// Config persistence / migration + I2C helper functions

uint32_t crc32Bytes(const uint8_t* data, size_t len);
uint32_t configCrc(const Config& c);
void setDefaults();
void saveConfig();
bool loadConfig();
bool validateConfig(bool logChanges = true);
void resetFilters();
bool i2cPresent(uint8_t addr);
bool readReg8(uint8_t addr, uint8_t reg, uint8_t& v);
bool readReg16(uint8_t addr, uint16_t reg, uint8_t& v);
void scanI2C();

const char* sensorName(uint8_t sensorType);
const char* geometryName();
