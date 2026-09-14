#pragma once
#include <Arduino.h>
#include <ESP8266WiFi.h>

// MQTT and diagnostics

void publishMqtt();
bool mqttResolveBroker(IPAddress& ip);
bool mqttTcpProbe(const IPAddress& ip);
void mqttPrintStatus();
void mqttDiagnosticTest();
bool connectMqtt();
void printBootDiagnostics();
void updateHeapDiag();
void printStorageDiagnostics();

const char* resetReasonShort();
