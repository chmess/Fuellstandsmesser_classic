/*
===============================================================================
 FUELLSTANDSMESSER_CLASSIC V0.12.3
 ESP8266 D1 mini | VL53L0X/VL53L1X | Nokia 5110 | MQTT
===============================================================================

GOAL
- Backport the proven Fuellstandsmesser3 measurement/tank logic
  to the ESP8266 D1 mini.
- Intentionally leaner than Fuellstandsmesser3: no large history, no BME,
  no OLED, no ESP32 NVS/Preferences.

KNOWN HARDWARE BASE
- ESP8266 D1 mini
- ToF I2C:
    SDA = D1
    SCL = D2
- Nokia 5110 / PCD8544: pins used D5, D4, D3, D6, D7

DISPLAY SIGNAL MAPPING IN THIS VERSION
- CLK = D5
- DIN = D7
- DC  = D4
- CS  = D6
- RST = D3

This mapping corresponds to the usual PCD8544 wiring with
D5 as clock and D7 as data. If the old Classic hardware maps
the five confirmed pins differently, only the five constants
below need to be changed.

LIBRARIES
- ESP8266 Arduino Core
- Adafruit GFX
- Adafruit PCD8544 Nokia 5110 LCD
- Adafruit VL53L0X
- Adafruit VL53L1X
- PubSubClient

MQTT COMPATIBILITY
- "average"     = current tank content in liters
- "fuellhoehe"  = current filtered ToF sensor distance in mm

===============================================================================
*/

#include <Arduino.h>
#include <stdarg.h>
#include <Wire.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <Updater.h>
#include <LittleFS.h>
#include <time.h>
#include <PubSubClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <Adafruit_VL53L0X.h>
#include <Adafruit_VL53L1X.h>
#include "Sensors.h"
#include "Measurement.h"
#include "Display.h"
#include "MqttDiagnostics.h"
#include "WifiManager.h"
#include "ConfigI2C.h"
#include "HistoryTypes.h"
#include "History.h"
#include "WebServerManager.h"
#include "AppRuntime.h"


// -----------------------------------------------------------------------------
// ARDUINO .INO PREPROCESSOR GUARD
// -----------------------------------------------------------------------------
// The Arduino preprocessor would otherwise generate invalid prototypes for functions
// whose struct types are defined later. These explicit declarations prevent that
// for the configuration migration helpers.
// Web output helpers: explicit prototypes required before History API.
// -----------------------------------------------------------------------------
// VERSION
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// PINOUT
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// HARDWARE / DEFAULTS
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// FUELLSTANDSMESSER3-COMPATIBLE DAILY HISTORY
// -----------------------------------------------------------------------------
// CSV/API/UI remain compatible with Fuellstandsmesser3.
// Internally, LittleFS remains appropriate for the ESP8266 hardware.
// Sources: 0=MEASURED, 1=IMPORTED, 2=TEST.

// -----------------------------------------------------------------------------
// CONFIG
// -----------------------------------------------------------------------------
#include "AppTypes.h"
#include "AppConstants.h"

Config cfg;

// -----------------------------------------------------------------------------
// OBJECTS
// -----------------------------------------------------------------------------
Adafruit_PCD8544 lcd(
  PIN_LCD_CLK,
  PIN_LCD_DIN,
  PIN_LCD_DC,
  PIN_LCD_CS,
  PIN_LCD_RST
);

Adafruit_VL53L0X vl53l0x;
Adafruit_VL53L1X vl53l1x;

ESP8266WebServer server(80);
DNSServer dnsServer;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// -----------------------------------------------------------------------------
// RUNTIME
// -----------------------------------------------------------------------------
bool apMode = false;
bool sensorOk = false;
uint8_t activeSensorType = SENSOR_AUTO;

uint32_t lastMeasureMs = 0;
uint32_t lastDisplayMs = 0;
uint32_t lastDisplayPageMs = 0;
uint8_t displayPage = 0;

bool displayPageAutoEnabled(uint8_t page) {
  if (page > 4) return false;
  return (cfg.displayPageMask & (1U << page)) != 0;
}

uint8_t displayNextAutoPage(uint8_t current) {
  for (uint8_t step = 1; step <= 5; ++step) {
    uint8_t candidate = (current + step) % 5;
    if (displayPageAutoEnabled(candidate)) return candidate;
  }
  return current;
}

uint32_t lastWifiRetryMs = 0;
uint32_t wifiLostSinceMs = 0;
uint32_t wifiReconnectCount = 0;
uint32_t wifiReconnectErrors = 0;
uint32_t minFreeHeapSeen = 0xFFFFFFFFUL;
bool webOtaActive = false;
bool webOtaSuccess = false;
uint32_t webOtaBytes = 0;
String webOtaError;
bool fsMounted = false;
FSInfo fsInfoCache;

uint32_t webRequestCount = 0;
uint32_t webLowHeapEvents = 0;
bool lowHeapActive = false;
uint32_t webOtaAttempts = 0;
uint32_t webOtaSuccessCount = 0;
uint32_t webOtaErrorCount = 0;
uint32_t lowestMaxBlockSeen = 0;
uint8_t highestHeapFragSeen = 0;
bool otaInProgress = false;
uint8_t otaLastPercent = 255;

uint32_t lastMqttRetryMs = 0;
uint32_t lastTofRetryMs = 0;

uint32_t mqttConnectCount = 0;
uint32_t mqttConnectErrors = 0;
uint32_t mqttRetryIntervalMs = MQTT_RETRY_MS;
uint32_t mqttLastConnectDurationMs = 0;
IPAddress mqttLastResolvedIp;
bool mqttLastDnsOk = false;
bool mqttLastTcpOk = false;
uint32_t mqttPublishCount = 0;
uint32_t mqttPublishErrors = 0;

float rawDistanceMm = NAN;
float filteredDistanceMm = NAN;
float tankHeightNowMm = NAN;
float tankPercent = NAN;
float tankLiters = NAN;

uint32_t measurementCount = 0;
uint32_t measurementErrors = 0;
uint32_t sensorRecoveries = 0;

bool ahtOk = false;                 // at least one current valid measurement
bool ahtInitialized = false;        // sensor ACK + initialization successful
float ahtTemperatureC = NAN;
float ahtHumidityPercent = NAN;
float ahtDewPointC = NAN;
uint32_t ahtReadCount = 0;
uint32_t ahtErrorCount = 0;
uint32_t ahtRecoveryCount = 0;
uint32_t ahtProbeCount = 0;
uint8_t ahtConsecutiveErrors = 0;
uint32_t lastAhtReadMs = 0;
uint32_t lastAhtRetryMs = 0;
uint32_t lastAhtValidMs = 0;
uint32_t lastAhtRecoveryMs = 0;
float ahtCondensationReserveC = NAN;

char cliLine[CLI_LINE_MAX];
size_t cliLineLen = 0;
uint32_t cliCommandCount = 0;
uint32_t cliErrorCount = 0;
bool cliWifiDirty = false;

float avgBuf[AVG_COUNT];
uint8_t avgPos = 0;
uint8_t avgUsed = 0;

float medBuf[MEDIAN_COUNT];
uint8_t medPos = 0;
uint8_t medUsed = 0;

float lastAcceptedDistance = NAN;

// Jump confirmation:
// A single large jump is rejected. Several similar new values
// are accepted as a genuine new level.
float pendingJumpDistance = NAN;
uint8_t pendingJumpCount = 0;


float startupCandidateDistance = NAN;
uint8_t startupCandidateCount = 0;
bool startupMeasurementStable = false;
