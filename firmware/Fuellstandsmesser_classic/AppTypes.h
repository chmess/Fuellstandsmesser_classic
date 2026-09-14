#pragma once
#include <Arduino.h>

// Shared data types used by real .cpp modules.
// Extracted unchanged from Fuellstandsmesser_classic V0.12.3.

struct Config {
  uint32_t magic;
  uint16_t version;

  char wifiSsid[33];
  char wifiPass[65];

  bool mqttEnabled;
  char mqttHost[65];
  uint16_t mqttPort;
  char mqttUser[33];
  char mqttPass[65];
  char mqttBase[65];
  char mqttAverageTopic[65];
  char mqttFuellhoeheTopic[65];

  uint8_t sensorType;      // 0 auto, 1 L0X, 2 L1X
  int16_t sensorOffsetMm;

  uint8_t geometry;        // 0 rectangle, 1 cylinder
  float tankLengthMm;
  float tankWidthMm;
  float tankHeightMm;
  float diameterMm;

  float emptyDistanceMm;
  float fullDistanceMm;

  uint32_t measurementIntervalMs;

  uint16_t minDistanceMm;
  uint16_t maxDistanceMm;
  uint16_t maxJumpMm;

  uint8_t displayContrast;
  bool displayAutoRotate;
  uint8_t displayPageSeconds;
  bool displayInvert;
  uint8_t displayPageMask;
  uint8_t displayFontWeight;   // 0=Normal, 1=Fett, 2=Extra-Fett

  bool haDiscoveryEnabled;
  char haDiscoveryPrefix[33];

  bool ahtEnabled;
  float ahtTemperatureOffsetC;
  float ahtHumidityOffsetPercent;
  uint32_t ahtIntervalMs;

  uint32_t crc;
};
