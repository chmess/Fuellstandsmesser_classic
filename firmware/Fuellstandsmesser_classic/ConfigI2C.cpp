#include "ConfigI2C.h"
#include "AppConstants.h"

#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>

extern Config cfg;

// Shared runtime state owned by the main sketch.
extern bool sensorOk;
extern uint8_t activeSensorType;

// Config constants / migration constants owned by the main sketch.


// I2C pins and device addresses owned by the main sketch.


// Cross-module functions that may be called from this module.
bool aht10Present();

// Local constants used by the config module.


extern float avgBuf[];
extern uint8_t avgPos;
extern uint8_t avgUsed;

extern float medBuf[];
extern uint8_t medPos;
extern uint8_t medUsed;

extern float lastAcceptedDistance;
extern float pendingJumpDistance;
extern uint8_t pendingJumpCount;
extern float startupCandidateDistance;
extern uint8_t startupCandidateCount;
extern bool startupMeasurementStable;


struct ConfigV6 {
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

  uint8_t sensorType;
  int16_t sensorOffsetMm;

  uint8_t geometry;
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
  uint8_t displayFontWeight;

  bool haDiscoveryEnabled;
  char haDiscoveryPrefix[33];

  uint32_t crc;
};

uint32_t configCrcV6(const ConfigV6& c) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&c);
  const size_t n = offsetof(ConfigV6, crc);
  uint32_t h = 2166136261UL;
  for (size_t i = 0; i < n; ++i) {
    h ^= p[i];
    h *= 16777619UL;
  }
  return h;
}

void applyAhtConfigDefaults() {
  cfg.ahtEnabled = true;
  cfg.ahtTemperatureOffsetC = 0.0f;
  cfg.ahtHumidityOffsetPercent = 0.0f;
  cfg.ahtIntervalMs = 10000UL;
}

void migrateConfigV6ToCurrent(const ConfigV6& oldCfg) {
  memset(&cfg, 0, sizeof(cfg));
  memcpy(&cfg, &oldCfg, offsetof(ConfigV6, crc));

  cfg.magic = CONFIG_MAGIC;
  cfg.version = CONFIG_VERSION;
  applyAhtConfigDefaults();
}

struct ConfigV5 {
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

  uint8_t sensorType;
  int16_t sensorOffsetMm;

  uint8_t geometry;
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
  uint8_t displayFontWeight;

  uint32_t crc;
};

uint32_t configCrcV5(const ConfigV5& c) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&c);
  const size_t n = offsetof(ConfigV5, crc);
  uint32_t h = 2166136261UL;
  for (size_t i = 0; i < n; ++i) {
    h ^= p[i];
    h *= 16777619UL;
  }
  return h;
}

void migrateConfigV5ToCurrent(const ConfigV5& oldCfg) {
  memset(&cfg, 0, sizeof(cfg));

  // V6 erweitert V5 nur am Ende vor CRC. Der gemeinsame Prefix bleibt gleich.
  memcpy(&cfg, &oldCfg, offsetof(ConfigV5, crc));

  cfg.magic = CONFIG_MAGIC;
  cfg.version = CONFIG_VERSION;
  cfg.haDiscoveryEnabled = false;
  strlcpy(cfg.haDiscoveryPrefix, "homeassistant", sizeof(cfg.haDiscoveryPrefix));
}

struct ConfigV1 {
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
  uint8_t sensorType;
  int16_t sensorOffsetMm;
  uint8_t geometry;
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
  uint32_t crc;
};

uint32_t configCrcV1(const ConfigV1& c) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&c);
  const size_t n = offsetof(ConfigV1, crc);
  uint32_t h = 2166136261UL;
  for (size_t i = 0; i < n; ++i) {
    h ^= p[i];
    h *= 16777619UL;
  }
  return h;
}

struct ConfigV2 {
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
  uint8_t sensorType;
  int16_t sensorOffsetMm;
  uint8_t geometry;
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
  uint32_t crc;
};

uint32_t configCrcV2(const ConfigV2& c) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&c);
  const size_t n = offsetof(ConfigV2, crc);
  uint32_t h = 2166136261UL;
  for (size_t i = 0; i < n; ++i) {
    h ^= p[i];
    h *= 16777619UL;
  }
  return h;
}

void migrateConfigV2ToCurrent(const ConfigV2& oldCfg) {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = CONFIG_MAGIC;
  cfg.version = CONFIG_VERSION;

  memcpy(cfg.wifiSsid, oldCfg.wifiSsid, sizeof(oldCfg.wifiSsid));
  memcpy(cfg.wifiPass, oldCfg.wifiPass, sizeof(oldCfg.wifiPass));
  cfg.mqttEnabled = oldCfg.mqttEnabled;
  memcpy(cfg.mqttHost, oldCfg.mqttHost, sizeof(oldCfg.mqttHost));
  cfg.mqttPort = oldCfg.mqttPort;
  memcpy(cfg.mqttUser, oldCfg.mqttUser, sizeof(oldCfg.mqttUser));
  memcpy(cfg.mqttPass, oldCfg.mqttPass, sizeof(oldCfg.mqttPass));
  memcpy(cfg.mqttBase, oldCfg.mqttBase, sizeof(oldCfg.mqttBase));
  memcpy(cfg.mqttAverageTopic, oldCfg.mqttAverageTopic, sizeof(oldCfg.mqttAverageTopic));
  memcpy(cfg.mqttFuellhoeheTopic, oldCfg.mqttFuellhoeheTopic, sizeof(oldCfg.mqttFuellhoeheTopic));

  cfg.sensorType = oldCfg.sensorType;
  cfg.sensorOffsetMm = oldCfg.sensorOffsetMm;
  cfg.geometry = oldCfg.geometry;
  cfg.tankLengthMm = oldCfg.tankLengthMm;
  cfg.tankWidthMm = oldCfg.tankWidthMm;
  cfg.tankHeightMm = oldCfg.tankHeightMm;
  cfg.diameterMm = oldCfg.diameterMm;
  cfg.emptyDistanceMm = oldCfg.emptyDistanceMm;
  cfg.fullDistanceMm = oldCfg.fullDistanceMm;
  cfg.measurementIntervalMs = oldCfg.measurementIntervalMs;
  cfg.minDistanceMm = oldCfg.minDistanceMm;
  cfg.maxDistanceMm = oldCfg.maxDistanceMm;
  cfg.maxJumpMm = oldCfg.maxJumpMm;
  cfg.displayContrast = oldCfg.displayContrast;
  cfg.displayAutoRotate = oldCfg.displayAutoRotate;
  cfg.displayPageSeconds = oldCfg.displayPageSeconds;
  cfg.displayInvert = false;
}


struct ConfigV4 {
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
  uint8_t sensorType;
  int16_t sensorOffsetMm;
  uint8_t geometry;
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
  uint32_t crc;
};

uint32_t configCrcV4(const ConfigV4& c) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&c);
  const size_t n = offsetof(ConfigV4, crc);
  uint32_t h = 2166136261UL;
  for (size_t i = 0; i < n; ++i) {
    h ^= p[i];
    h *= 16777619UL;
  }
  return h;
}

void migrateConfigV4ToCurrent(const ConfigV4& oldCfg) {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = CONFIG_MAGIC;
  cfg.version = CONFIG_VERSION;

  memcpy(cfg.wifiSsid, oldCfg.wifiSsid, sizeof(oldCfg.wifiSsid));
  memcpy(cfg.wifiPass, oldCfg.wifiPass, sizeof(oldCfg.wifiPass));
  cfg.mqttEnabled = oldCfg.mqttEnabled;
  memcpy(cfg.mqttHost, oldCfg.mqttHost, sizeof(oldCfg.mqttHost));
  cfg.mqttPort = oldCfg.mqttPort;
  memcpy(cfg.mqttUser, oldCfg.mqttUser, sizeof(oldCfg.mqttUser));
  memcpy(cfg.mqttPass, oldCfg.mqttPass, sizeof(oldCfg.mqttPass));
  memcpy(cfg.mqttBase, oldCfg.mqttBase, sizeof(oldCfg.mqttBase));
  memcpy(cfg.mqttAverageTopic, oldCfg.mqttAverageTopic, sizeof(oldCfg.mqttAverageTopic));
  memcpy(cfg.mqttFuellhoeheTopic, oldCfg.mqttFuellhoeheTopic, sizeof(oldCfg.mqttFuellhoeheTopic));

  cfg.sensorType = oldCfg.sensorType;
  cfg.sensorOffsetMm = oldCfg.sensorOffsetMm;
  cfg.geometry = oldCfg.geometry;
  cfg.tankLengthMm = oldCfg.tankLengthMm;
  cfg.tankWidthMm = oldCfg.tankWidthMm;
  cfg.tankHeightMm = oldCfg.tankHeightMm;
  cfg.diameterMm = oldCfg.diameterMm;
  cfg.emptyDistanceMm = oldCfg.emptyDistanceMm;
  cfg.fullDistanceMm = oldCfg.fullDistanceMm;
  cfg.measurementIntervalMs = oldCfg.measurementIntervalMs;
  cfg.minDistanceMm = oldCfg.minDistanceMm;
  cfg.maxDistanceMm = oldCfg.maxDistanceMm;
  cfg.maxJumpMm = oldCfg.maxJumpMm;
  cfg.displayContrast = oldCfg.displayContrast;
  cfg.displayAutoRotate = oldCfg.displayAutoRotate;
  cfg.displayPageSeconds = oldCfg.displayPageSeconds;
  cfg.displayInvert = oldCfg.displayInvert;
  cfg.displayPageMask = oldCfg.displayPageMask;
  cfg.displayFontWeight = 1;  // bisherige V0.7.6-Darstellung = Fett
}

struct ConfigV3 {
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
  uint8_t sensorType;
  int16_t sensorOffsetMm;
  uint8_t geometry;
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
  uint32_t crc;
};

uint32_t configCrcV3(const ConfigV3& c) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&c);
  const size_t n = offsetof(ConfigV3, crc);
  uint32_t h = 2166136261UL;
  for (size_t i = 0; i < n; ++i) {
    h ^= p[i];
    h *= 16777619UL;
  }
  return h;
}

void migrateConfigV3ToCurrent(const ConfigV3& oldCfg) {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = CONFIG_MAGIC;
  cfg.version = CONFIG_VERSION;

  memcpy(cfg.wifiSsid, oldCfg.wifiSsid, sizeof(oldCfg.wifiSsid));
  memcpy(cfg.wifiPass, oldCfg.wifiPass, sizeof(oldCfg.wifiPass));
  cfg.mqttEnabled = oldCfg.mqttEnabled;
  memcpy(cfg.mqttHost, oldCfg.mqttHost, sizeof(oldCfg.mqttHost));
  cfg.mqttPort = oldCfg.mqttPort;
  memcpy(cfg.mqttUser, oldCfg.mqttUser, sizeof(oldCfg.mqttUser));
  memcpy(cfg.mqttPass, oldCfg.mqttPass, sizeof(oldCfg.mqttPass));
  memcpy(cfg.mqttBase, oldCfg.mqttBase, sizeof(oldCfg.mqttBase));
  memcpy(cfg.mqttAverageTopic, oldCfg.mqttAverageTopic, sizeof(oldCfg.mqttAverageTopic));
  memcpy(cfg.mqttFuellhoeheTopic, oldCfg.mqttFuellhoeheTopic, sizeof(oldCfg.mqttFuellhoeheTopic));

  cfg.sensorType = oldCfg.sensorType;
  cfg.sensorOffsetMm = oldCfg.sensorOffsetMm;
  cfg.geometry = oldCfg.geometry;
  cfg.tankLengthMm = oldCfg.tankLengthMm;
  cfg.tankWidthMm = oldCfg.tankWidthMm;
  cfg.tankHeightMm = oldCfg.tankHeightMm;
  cfg.diameterMm = oldCfg.diameterMm;
  cfg.emptyDistanceMm = oldCfg.emptyDistanceMm;
  cfg.fullDistanceMm = oldCfg.fullDistanceMm;
  cfg.measurementIntervalMs = oldCfg.measurementIntervalMs;
  cfg.minDistanceMm = oldCfg.minDistanceMm;
  cfg.maxDistanceMm = oldCfg.maxDistanceMm;
  cfg.maxJumpMm = oldCfg.maxJumpMm;
  cfg.displayContrast = oldCfg.displayContrast;
  cfg.displayAutoRotate = oldCfg.displayAutoRotate;
  cfg.displayPageSeconds = oldCfg.displayPageSeconds;
  cfg.displayInvert = oldCfg.displayInvert;
  cfg.displayPageMask = 0x0F;
  cfg.displayFontWeight = 1;
}


// -----------------------------------------------------------------------------
// HELPERS
// -----------------------------------------------------------------------------
uint32_t crc32Bytes(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFUL;
  while (len--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; i++) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1)));
    }
  }
  return ~crc;
}

uint32_t configCrc(const Config& c) {
  return crc32Bytes(
    reinterpret_cast<const uint8_t*>(&c),
    offsetof(Config, crc)
  );
}

void setDefaults() {
  memset(&cfg, 0, sizeof(cfg));

  cfg.magic = CONFIG_MAGIC;
  cfg.version = CONFIG_VERSION;

  cfg.wifiSsid[0] = 0;
  cfg.wifiPass[0] = 0;

  cfg.mqttEnabled = true;
  strlcpy(cfg.mqttHost, "192.168.0.10", sizeof(cfg.mqttHost));
  cfg.mqttPort = 1883;
  cfg.mqttUser[0] = 0;
  cfg.mqttPass[0] = 0;
  strlcpy(cfg.mqttBase, "Fuellstandsmesser_classic", sizeof(cfg.mqttBase));
  strlcpy(cfg.mqttAverageTopic, "average", sizeof(cfg.mqttAverageTopic));
  strlcpy(cfg.mqttFuellhoeheTopic, "fuellhoehe", sizeof(cfg.mqttFuellhoeheTopic));
  cfg.haDiscoveryEnabled = false;
  strlcpy(cfg.haDiscoveryPrefix, "homeassistant", sizeof(cfg.haDiscoveryPrefix));
  applyAhtConfigDefaults();

  cfg.sensorType = SENSOR_AUTO;
  cfg.sensorOffsetMm = 0;

  cfg.geometry = GEOMETRY_RECT;
  cfg.tankLengthMm = 1000.0f;
  cfg.tankWidthMm = 1000.0f;
  cfg.tankHeightMm = 1500.0f;
  cfg.diameterMm = 1000.0f;

  cfg.emptyDistanceMm = 1500.0f;
  cfg.fullDistanceMm = 100.0f;

  cfg.measurementIntervalMs = 20000UL;
  cfg.minDistanceMm = 30;
  cfg.maxDistanceMm = 4000;
  cfg.maxJumpMm = 500;

  cfg.displayContrast = 55;
  cfg.displayAutoRotate = true;
  cfg.displayPageSeconds = 5;
  cfg.displayInvert = false;
  cfg.displayPageMask = 0x0F;
  cfg.displayFontWeight = 1;

  cfg.crc = configCrc(cfg);
}

bool validateConfig(bool logChanges);

void saveConfig() {
  cfg.magic = CONFIG_MAGIC;
  cfg.version = CONFIG_VERSION;
  cfg.crc = configCrc(cfg);

  EEPROM.put(0, cfg);
  EEPROM.commit();

  Serial.println(F("[CONFIG] gespeichert"));
}

bool loadConfig() {
  EEPROM.get(0, cfg);

  if (cfg.magic == CONFIG_MAGIC &&
      cfg.version == CONFIG_VERSION &&
      cfg.crc == configCrc(cfg)) {
    Serial.println(F("[CONFIG] geladen V7"));
    return true;
  }

  ConfigV6 v6;
  EEPROM.get(0, v6);
  if (v6.magic == CONFIG_MAGIC &&
      v6.version == CONFIG_VERSION_V6 &&
      v6.crc == configCrcV6(v6)) {
    Serial.println(F("[CONFIG] V6 erkannt -> Migration auf V7"));
    migrateConfigV6ToCurrent(v6);
    validateConfig(false);
    saveConfig();
    Serial.println(F("[CONFIG] Migration V6 -> V7 abgeschlossen"));
    return true;
  }

  ConfigV5 v5;
  EEPROM.get(0, v5);
  if (v5.magic == CONFIG_MAGIC &&
      v5.version == CONFIG_VERSION_V5 &&
      v5.crc == configCrcV5(v5)) {
    Serial.println(F("[CONFIG] V5 erkannt -> Migration auf V7"));
    migrateConfigV5ToCurrent(v5);
    applyAhtConfigDefaults();
    validateConfig(false);
    saveConfig();
    Serial.println(F("[CONFIG] Migration V5 -> V7 abgeschlossen"));
    return true;
  }

  ConfigV4 v4;
  EEPROM.get(0, v4);
  if (v4.magic == CONFIG_MAGIC &&
      v4.version == CONFIG_VERSION_V4 &&
      v4.crc == configCrcV4(v4)) {
    Serial.println(F("[CONFIG] V4 erkannt -> Migration auf V7"));
    migrateConfigV4ToCurrent(v4);
    applyAhtConfigDefaults();
    cfg.haDiscoveryEnabled = false;
    strlcpy(cfg.haDiscoveryPrefix, "homeassistant", sizeof(cfg.haDiscoveryPrefix));
    validateConfig(false);
    saveConfig();
    Serial.println(F("[CONFIG] Migration V4 -> V7 abgeschlossen"));
    return true;
  }

  ConfigV3 v3;
  EEPROM.get(0, v3);
  if (v3.magic == CONFIG_MAGIC &&
      v3.version == CONFIG_VERSION_V3 &&
      v3.crc == configCrcV3(v3)) {
    Serial.println(F("[CONFIG] V3 erkannt -> Migration auf V7"));
    migrateConfigV3ToCurrent(v3);
    applyAhtConfigDefaults();
    cfg.haDiscoveryEnabled = false;
    strlcpy(cfg.haDiscoveryPrefix, "homeassistant", sizeof(cfg.haDiscoveryPrefix));
    cfg.displayFontWeight = 1;
    validateConfig(false);
    saveConfig();
    Serial.println(F("[CONFIG] Migration V3 -> V7 abgeschlossen"));
    return true;
  }

  ConfigV2 v2;
  EEPROM.get(0, v2);
  if (v2.magic == CONFIG_MAGIC &&
      v2.version == CONFIG_VERSION_V2 &&
      v2.crc == configCrcV2(v2)) {
    Serial.println(F("[CONFIG] V2 erkannt -> Migration auf V7"));
    migrateConfigV2ToCurrent(v2);
    applyAhtConfigDefaults();
    cfg.haDiscoveryEnabled = false;
    strlcpy(cfg.haDiscoveryPrefix, "homeassistant", sizeof(cfg.haDiscoveryPrefix));
    cfg.displayPageMask = 0x0F;
    cfg.displayFontWeight = 1;
    validateConfig(false);
    saveConfig();
    Serial.println(F("[CONFIG] Migration V2 -> V7 abgeschlossen"));
    return true;
  }

  ConfigV1 oldCfg;
  EEPROM.get(0, oldCfg);
  if (oldCfg.magic == CONFIG_MAGIC &&
      oldCfg.version == CONFIG_VERSION_LEGACY &&
      oldCfg.crc == configCrcV1(oldCfg)) {
    Serial.println(F("[CONFIG] V1 erkannt -> Migration auf V7"));

    memset(&cfg, 0, sizeof(cfg));
    cfg.magic = CONFIG_MAGIC;
    cfg.version = CONFIG_VERSION;

    memcpy(cfg.wifiSsid, oldCfg.wifiSsid, sizeof(oldCfg.wifiSsid));
    memcpy(cfg.wifiPass, oldCfg.wifiPass, sizeof(oldCfg.wifiPass));
    cfg.mqttEnabled = oldCfg.mqttEnabled;
    memcpy(cfg.mqttHost, oldCfg.mqttHost, sizeof(oldCfg.mqttHost));
    cfg.mqttPort = oldCfg.mqttPort;
    memcpy(cfg.mqttUser, oldCfg.mqttUser, sizeof(oldCfg.mqttUser));
    memcpy(cfg.mqttPass, oldCfg.mqttPass, sizeof(oldCfg.mqttPass));
    memcpy(cfg.mqttBase, oldCfg.mqttBase, sizeof(oldCfg.mqttBase));
    memcpy(cfg.mqttAverageTopic, oldCfg.mqttAverageTopic, sizeof(oldCfg.mqttAverageTopic));
    memcpy(cfg.mqttFuellhoeheTopic, oldCfg.mqttFuellhoeheTopic, sizeof(oldCfg.mqttFuellhoeheTopic));

    cfg.sensorType = oldCfg.sensorType;
    cfg.sensorOffsetMm = oldCfg.sensorOffsetMm;
    cfg.geometry = oldCfg.geometry;
    cfg.tankLengthMm = oldCfg.tankLengthMm;
    cfg.tankWidthMm = oldCfg.tankWidthMm;
    cfg.tankHeightMm = oldCfg.tankHeightMm;
    cfg.diameterMm = oldCfg.diameterMm;
    cfg.emptyDistanceMm = oldCfg.emptyDistanceMm;
    cfg.fullDistanceMm = oldCfg.fullDistanceMm;
    cfg.measurementIntervalMs = oldCfg.measurementIntervalMs;
    cfg.minDistanceMm = oldCfg.minDistanceMm;
    cfg.maxDistanceMm = oldCfg.maxDistanceMm;
    cfg.maxJumpMm = oldCfg.maxJumpMm;
    cfg.displayContrast = oldCfg.displayContrast;
    cfg.displayAutoRotate = true;
    cfg.displayPageSeconds = 5;
    cfg.displayInvert = false;
    cfg.displayPageMask = 0x0F;
    cfg.displayFontWeight = 1;
    cfg.haDiscoveryEnabled = false;
    strlcpy(cfg.haDiscoveryPrefix, "homeassistant", sizeof(cfg.haDiscoveryPrefix));
    applyAhtConfigDefaults();

    validateConfig(false);
    saveConfig();
    Serial.println(F("[CONFIG] Migration V1 -> V7 abgeschlossen"));
    return true;
  }

  Serial.println(F("[CONFIG] ungueltig -> Defaults"));
  setDefaults();
  saveConfig();
  return false;
}

bool validateConfig(bool logChanges) {
  bool changed = false;

  auto logFix = [&](const __FlashStringHelper* msg) {
    if (logChanges) {
      Serial.print(F("[CONFIG] Korrektur: "));
      Serial.println(msg);
    }
    changed = true;
  };

  if (cfg.haDiscoveryPrefix[0] == '\0') {
    strlcpy(cfg.haDiscoveryPrefix, "homeassistant", sizeof(cfg.haDiscoveryPrefix));
    logFix(F("HA Discovery Prefix -> homeassistant"));
  }

  for(size_t i=0; cfg.haDiscoveryPrefix[i]; ++i){
    const char c=cfg.haDiscoveryPrefix[i];
    const bool ok=
      (c>='a'&&c<='z') ||
      (c>='A'&&c<='Z') ||
      (c>='0'&&c<='9') ||
      c=='_' || c=='-' || c=='/';
    if(!ok){
      strlcpy(cfg.haDiscoveryPrefix, "homeassistant", sizeof(cfg.haDiscoveryPrefix));
      logFix(F("HA Discovery Prefix ungueltig -> homeassistant"));
      break;
    }
  }

  if (!isfinite(cfg.ahtTemperatureOffsetC) ||
      cfg.ahtTemperatureOffsetC < -20.0f ||
      cfg.ahtTemperatureOffsetC > 20.0f) {
    cfg.ahtTemperatureOffsetC = 0.0f;
    logFix(F("AHT Temperatur-Offset -> 0.0 C"));
  }

  if (!isfinite(cfg.ahtHumidityOffsetPercent) ||
      cfg.ahtHumidityOffsetPercent < -50.0f ||
      cfg.ahtHumidityOffsetPercent > 50.0f) {
    cfg.ahtHumidityOffsetPercent = 0.0f;
    logFix(F("AHT Feuchte-Offset -> 0.0 %"));
  }

  if (cfg.ahtIntervalMs < 2000UL || cfg.ahtIntervalMs > 300000UL) {
    cfg.ahtIntervalMs = 10000UL;
    logFix(F("AHT Messintervall -> 10000 ms"));
  }

  if (cfg.sensorType > SENSOR_VL53L1X) {
    cfg.sensorType = SENSOR_AUTO;
    logFix(F("Sensorwahl -> AUTO"));
  }

  if (cfg.geometry > GEOMETRY_CYLINDER) {
    cfg.geometry = GEOMETRY_RECT;
    logFix(F("Tankgeometrie -> QUADER"));
  }

  if (!isfinite(cfg.tankLengthMm) || cfg.tankLengthMm < 100.0f || cfg.tankLengthMm > 20000.0f) {
    cfg.tankLengthMm = 1000.0f;
    logFix(F("Tanklaenge -> 1000 mm"));
  }

  if (!isfinite(cfg.tankWidthMm) || cfg.tankWidthMm < 100.0f || cfg.tankWidthMm > 20000.0f) {
    cfg.tankWidthMm = 1000.0f;
    logFix(F("Tankbreite -> 1000 mm"));
  }

  if (!isfinite(cfg.tankHeightMm) || cfg.tankHeightMm < 100.0f || cfg.tankHeightMm > 10000.0f) {
    cfg.tankHeightMm = 1500.0f;
    logFix(F("Tankhoehe -> 1500 mm"));
  }

  if (!isfinite(cfg.diameterMm) || cfg.diameterMm < 100.0f || cfg.diameterMm > 20000.0f) {
    cfg.diameterMm = 1000.0f;
    logFix(F("Tankdurchmesser -> 1000 mm"));
  }

  if (!isfinite(cfg.fullDistanceMm) || cfg.fullDistanceMm < 20.0f || cfg.fullDistanceMm > 10000.0f) {
    cfg.fullDistanceMm = 100.0f;
    logFix(F("Voll-Distanz -> 100 mm"));
  }

  if (!isfinite(cfg.emptyDistanceMm) || cfg.emptyDistanceMm <= cfg.fullDistanceMm + 20.0f ||
      cfg.emptyDistanceMm > 10000.0f) {
    cfg.emptyDistanceMm = 1500.0f;
    if (cfg.emptyDistanceMm <= cfg.fullDistanceMm + 20.0f) {
      cfg.fullDistanceMm = 100.0f;
    }
    logFix(F("Leer-/Voll-Distanz korrigiert"));
  }

  if (cfg.measurementIntervalMs < 500UL) {
    cfg.measurementIntervalMs = 500UL;
    logFix(F("Messintervall -> 500 ms"));
  }
  if (cfg.measurementIntervalMs > 3600000UL) {
    cfg.measurementIntervalMs = 3600000UL;
    logFix(F("Messintervall -> 3600000 ms"));
  }

  if (cfg.minDistanceMm < 20) {
    cfg.minDistanceMm = 20;
    logFix(F("Min-Abstand -> 20 mm"));
  }

  if (cfg.maxDistanceMm <= cfg.minDistanceMm) {
    cfg.maxDistanceMm = cfg.minDistanceMm + 100;
    logFix(F("Max-Abstand korrigiert"));
  }

  if (cfg.maxDistanceMm > 8000) {
    cfg.maxDistanceMm = 8000;
    logFix(F("Max-Abstand -> 8000 mm"));
  }

  if (cfg.maxJumpMm < 10) {
    cfg.maxJumpMm = 10;
    logFix(F("Sprunggrenze -> 10 mm"));
  }

  if (cfg.maxJumpMm > 5000) {
    cfg.maxJumpMm = 5000;
    logFix(F("Sprunggrenze -> 5000 mm"));
  }

  if (cfg.mqttPort == 0) {
    cfg.mqttPort = 1883;
    logFix(F("MQTT-Port -> 1883"));
  }

  if (cfg.displayContrast < 20 || cfg.displayContrast > 100) {
    cfg.displayContrast = 55;
    logFix(F("LCD-Kontrast -> 55"));
  }

  if (cfg.displayPageSeconds < 2 || cfg.displayPageSeconds > 60) {
    cfg.displayPageSeconds = 5;
    logFix(F("LCD-Seitenintervall -> 5 s"));
  }

  cfg.displayPageMask &= 0x1F;
  if (cfg.displayPageMask == 0) {
    cfg.displayPageMask = 0x01;
    logFix(F("LCD-Seitenmaske -> Seite 1"));
  }

  if (cfg.displayFontWeight > 2) {
    cfg.displayFontWeight = 1;
    logFix(F("LCD-Schriftstaerke -> Fett"));
  }

  // Strings immer sicher terminieren.
  cfg.wifiSsid[sizeof(cfg.wifiSsid)-1] = 0;
  cfg.wifiPass[sizeof(cfg.wifiPass)-1] = 0;
  cfg.mqttHost[sizeof(cfg.mqttHost)-1] = 0;
  cfg.mqttUser[sizeof(cfg.mqttUser)-1] = 0;
  cfg.mqttPass[sizeof(cfg.mqttPass)-1] = 0;
  cfg.mqttBase[sizeof(cfg.mqttBase)-1] = 0;
  cfg.mqttAverageTopic[sizeof(cfg.mqttAverageTopic)-1] = 0;
  cfg.mqttFuellhoeheTopic[sizeof(cfg.mqttFuellhoeheTopic)-1] = 0;

  return changed;
}

void resetFilters() {
  for (uint8_t i = 0; i < AVG_COUNT; i++) avgBuf[i] = 0;
  for (uint8_t i = 0; i < MEDIAN_COUNT; i++) medBuf[i] = 0;
  avgPos = avgUsed = 0;
  medPos = medUsed = 0;
  lastAcceptedDistance = NAN;
  pendingJumpDistance = NAN;
  pendingJumpCount = 0;

  startupCandidateDistance = NAN;
  startupCandidateCount = 0;
  startupMeasurementStable = false;
}

const char* sensorName(uint8_t t) {
  switch (t) {
    case SENSOR_VL53L0X: return "VL53L0X";
    case SENSOR_VL53L1X: return "VL53L1X";
    default: return "AUTO";
  }
}

const char* geometryName() {
  return cfg.geometry == GEOMETRY_CYLINDER ? "Zylinder" : "Quader";
}

bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

bool readReg8(uint8_t addr, uint8_t reg, uint8_t& v) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr, 1) != 1) return false;
  v = Wire.read();
  return true;
}

bool readReg16(uint8_t addr, uint16_t reg, uint8_t& v) {
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr, 1) != 1) return false;
  v = Wire.read();
  return true;
}

void scanI2C() {
  Serial.println(F("[I2C] Scan Start"));
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.print(F("[I2C] gefunden: 0x"));
      if (addr < 16) Serial.print('0');
      Serial.println(addr, HEX);
      found++;
    }
    yield();
  }
  if (found == 0) Serial.println(F("[I2C] KEIN GERAET GEFUNDEN"));
  Serial.println(F("[I2C] Scan Ende"));
}
