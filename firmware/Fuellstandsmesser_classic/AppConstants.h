#pragma once
#include <Arduino.h>
#include "Language.h"

// -----------------------------------------------------------------------------
// Firmware
// -----------------------------------------------------------------------------
#ifndef FW_VERSION
#define FW_VERSION "V0.12.3"
#endif

// -----------------------------------------------------------------------------
// Hardware pins
// -----------------------------------------------------------------------------
static constexpr uint8_t PIN_I2C_SDA = D2;
static constexpr uint8_t PIN_I2C_SCL = D1;

static constexpr uint8_t PIN_LCD_CLK = D5;
static constexpr uint8_t PIN_LCD_DIN = D7;
static constexpr uint8_t PIN_LCD_DC  = D4;
static constexpr uint8_t PIN_LCD_CS  = D6;
static constexpr uint8_t PIN_LCD_RST = D3;

// -----------------------------------------------------------------------------
// Sensor / I2C addresses
// -----------------------------------------------------------------------------
static constexpr uint8_t VL53_ADDR  = 0x29;
static constexpr uint8_t AHT10_ADDR = 0x38;

static constexpr uint8_t SENSOR_AUTO    = 0;
static constexpr uint8_t SENSOR_VL53L0X = 1;
static constexpr uint8_t SENSOR_VL53L1X = 2;

static constexpr uint8_t GEOMETRY_RECT     = 0;
static constexpr uint8_t GEOMETRY_CYLINDER = 1;

// -----------------------------------------------------------------------------
// Configuration format
// -----------------------------------------------------------------------------
static constexpr uint32_t CONFIG_MAGIC   = 0x46434C31UL; // "FCL1"
static constexpr uint16_t CONFIG_VERSION = 7;

static constexpr uint16_t CONFIG_VERSION_V6     = 6;
static constexpr uint16_t CONFIG_VERSION_V5     = 5;
static constexpr uint16_t CONFIG_VERSION_V4     = 4;
static constexpr uint16_t CONFIG_VERSION_V3     = 3;
static constexpr uint16_t CONFIG_VERSION_V2     = 2;
static constexpr uint16_t CONFIG_VERSION_LEGACY = 1;

// -----------------------------------------------------------------------------
// Timing
// -----------------------------------------------------------------------------
static constexpr uint32_t AHT_READ_MS   = 10000UL;
static constexpr uint32_t AHT_RETRY_MS  = 5000UL;
static constexpr uint32_t AHT_STALE_MS  = 30000UL;
static constexpr uint32_t TOF_RETRY_MS  = 15000UL;

static constexpr uint32_t WIFI_RETRY_MS       = 30000UL;
static constexpr uint32_t WIFI_AP_FALLBACK_MS = 120000UL;

static constexpr uint32_t MQTT_RETRY_MS     = 10000UL;
static constexpr uint32_t MQTT_RETRY_MAX_MS = 60000UL;
static constexpr uint16_t MQTT_BUFFER_NORMAL = 384;

static constexpr uint32_t DISPLAY_MS      = 1000UL;
static constexpr uint32_t DISPLAY_PAGE_MS = 5000UL;

// -----------------------------------------------------------------------------
// Measurement filters
// -----------------------------------------------------------------------------
static constexpr uint8_t AVG_COUNT    = 5;
static constexpr uint8_t MEDIAN_COUNT = 5;

static constexpr uint8_t JUMP_CONFIRM_COUNT = 3;
static constexpr float JUMP_CONFIRM_TOLERANCE_MM = 80.0f;

static constexpr uint8_t STARTUP_CONFIRM_COUNT = 3;
static constexpr float STARTUP_CONFIRM_TOLERANCE_MM = 40.0f;

static constexpr float AHT_SMOOTH_ALPHA = 0.25f;

// -----------------------------------------------------------------------------
// CLI / storage
// -----------------------------------------------------------------------------
static constexpr size_t CLI_LINE_MAX = 192;
static constexpr uint32_t HISTORY_RESERVE_BYTES = 32768UL;