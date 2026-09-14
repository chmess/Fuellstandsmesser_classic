#pragma once
#include <Arduino.h>
#include <ESP8266WebServer.h>

// Web/API/OTA user interface

void noteWebRequest();
void webPrepareConnectionClose();
void webFinishConnection();
void webSendSafe(const String& value);
void webSendSafe(const char* value);
void webSendUInt(uint32_t value);
void webSendInt(int32_t value);
void webSendFloat(float value,uint8_t decimals);
void webStreamBegin(const __FlashStringHelper* title);
void webStreamNav(uint8_t active);
void webStreamEnd();
void historyBuildStatsCache();
float historyConsumptionDays(uint16_t days);
void handleRoot();
String checked(bool v);
void handleSettings();
void copyArg(const char* name, char* dst, size_t len);
void handleSave();
void handleNotFound();
void jsonChunkBegin();
void jsonChunkEnd();
void jsonSendKey(const __FlashStringHelper* key);
void jsonSendStringValue(const String& value);
void jsonSendStringValue(const char* value);
void jsonSendBoolValue(bool value);
void jsonSendNumberValue(const String& value);
void jsonSendUIntValue(uint32_t value);
void jsonSendIntValue(int32_t value);
void jsonSendFloatValue(float value,uint8_t decimals=1);
void handleApiStatus();
void handleHealthApi();
void handleWebOtaPage();
void handleWebOtaUpload();
void handleWebOtaDone();
void webMetricCard(const __FlashStringHelper* title,const String& value);
void webMetricCardUInt(const __FlashStringHelper* title,uint32_t value,const __FlashStringHelper* suffix=nullptr);
void webMetricCardFloat(const __FlashStringHelper* title,float value,uint8_t decimals,const __FlashStringHelper* suffix=nullptr);
void webTableRow(const __FlashStringHelper* label,const String& value);
void handleSystemStatusPage();
uint32_t storageRemoveKnownTempFile(const char* path);
void handleStorageCleanupTemp();
void handleStorageStatus();
void handleFactoryReset();
void handleDisplayPageApi();
void setupWeb();
