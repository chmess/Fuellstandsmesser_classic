#pragma once
#include <Arduino.h>

// CLI + application runtime

void cliPrintPrompt();
void cliPrintHelp();
void cliPrintWifiStatus();
void cliWifiScan();
void cliStartConfiguredWifi();
void cliExecute(char* line);
void handleSerialCli();
void setup();
void loop();
