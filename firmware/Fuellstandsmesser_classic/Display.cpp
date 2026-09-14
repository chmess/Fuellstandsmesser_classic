#include "Display.h"
#include "AppConstants.h"
#include "AppTypes.h"
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <math.h>

extern Config cfg;
extern Adafruit_PCD8544 lcd;
extern PubSubClient mqttClient;
extern bool sensorOk;
extern uint8_t activeSensorType;
extern bool apMode;
extern float rawDistanceMm;
extern float filteredDistanceMm;
extern float tankHeightNowMm;
extern float tankPercent;
extern float tankLiters;
extern uint32_t measurementErrors;
extern uint32_t sensorRecoveries;
extern uint32_t minFreeHeapSeen;
extern uint8_t displayPage;
extern bool ahtOk;
extern float ahtTemperatureC;
extern float ahtHumidityPercent;
extern float ahtDewPointC;
extern float ahtCondensationReserveC;

const char* sensorName(uint8_t t);


// -----------------------------------------------------------------------------
// DISPLAY
// -----------------------------------------------------------------------------
int8_t lcdTextXOffset = 0;
int8_t lcdTextYOffset = 0;

inline void lcdCursor(int16_t x, int16_t y) {
  lcd.setCursor(x + lcdTextXOffset, y + lcdTextYOffset);
}

void drawTankBar(int x, int y, int w, int h, float pct) {
  lcd.drawRect(x, y, w, h, BLACK);

  if (!isfinite(pct)) return;

  int innerH = h - 2;
  int fillH = (int)roundf(innerH * constrain(pct, 0.0f, 100.0f) / 100.0f);
  if (fillH > 0) {
    lcd.fillRect(x + 1, y + h - 1 - fillH, w - 2, fillH, BLACK);
  }
}

void drawDisplayPageMain() {
  // Page 1: maximize readability of the two most important values.
  lcd.setTextSize(1);
  lcdCursor(0, 0);
  lcd.print(TR("TANK","TANK"));

  if (isfinite(tankPercent)) {
    lcdCursor(28, 0);
    lcd.print(tankPercent, 0);
    lcd.print(F("%"));
  } else {
    lcdCursor(28, 0);
    lcd.print(F("--%"));
  }

  lcd.setTextSize(2);
  lcdCursor(0, 11);
  if (isfinite(tankLiters)) {
    lcd.print(tankLiters, 0);
  } else {
    lcd.print(F("----"));
  }

  lcd.setTextSize(1);
  lcd.print(F("L"));

  // Bottom line contains only the two technically relevant values.
  lcdCursor(0, 34);
  lcd.print(F("H"));
  if (isfinite(tankHeightNowMm)) lcd.print(tankHeightNowMm, 0);
  else lcd.print(F("--"));
  lcd.print(F("mm"));

  lcdCursor(44, 34);
  lcd.print(F("D"));
  if (isfinite(filteredDistanceMm)) lcd.print(filteredDistanceMm, 0);
  else lcd.print(F("--"));

  // Small horizontal level indicator across the full width.
  const int x=1,y=44,w=82,h=4;
  lcd.drawRect(x,y,w,h,BLACK);
  if(isfinite(tankPercent)){
    int fill=(int)round((w-2)*constrain(tankPercent,0.0f,100.0f)/100.0f);
    if(fill>0)lcd.fillRect(x+1,y+1,fill,h-2,BLACK);
  }
}

void drawDisplayPageSensor() {
  // Page 2: filtered value large, diagnostics small.
  lcd.setTextSize(1);
  lcdCursor(0, 0);
  lcd.print(sensorName(activeSensorType));

  lcd.setTextSize(2);
  lcdCursor(0, 11);
  if (isfinite(filteredDistanceMm)) {
    lcd.print(filteredDistanceMm, 0);
  } else {
    lcd.print(F("---"));
  }

  lcd.setTextSize(1);
  lcd.print(F("mm"));

  lcdCursor(0, 33);
  lcd.print(F("RAW "));
  if (isfinite(rawDistanceMm)) lcd.print(rawDistanceMm, 0);
  else lcd.print(F("--"));

  lcdCursor(0, 43);
  lcd.print(F("E"));
  lcd.print(measurementErrors);
  lcd.print(F(" R"));
  lcd.print(sensorRecoveries);

  lcdCursor(52, 43);
  lcd.print(sensorOk ? F("OK") : F("ERR"));
}

void drawDisplayPageNetwork() {
  // Page 3: connection status at a glance.
  lcd.setTextSize(1);
  lcdCursor(0, 0);
  lcd.print(TR("NETZ","NETWORK"));

  lcdCursor(34, 0);
  if (WiFi.status() == WL_CONNECTED) lcd.print(TR("WIFI OK","WIFI OK"));
  else if (apMode) lcd.print(F("AP"));
  else lcd.print(F("WIFI --"));

  lcd.setTextSize(1);
  lcdCursor(0, 12);
  if (WiFi.status() == WL_CONNECTED) {
    lcd.print(WiFi.localIP().toString());
  } else if (apMode) {
    lcd.print(WiFi.softAPIP().toString());
  } else {
    lcd.print(TR("keine IP","no IP"));
  }

  lcdCursor(0, 25);
  lcd.print(F("RSSI "));
  if(WiFi.status()==WL_CONNECTED){
    lcd.print(WiFi.RSSI());
    lcd.print(F("dBm"));
  } else {
    lcd.print(F("--"));
  }

  lcdCursor(0, 38);
  lcd.print(F("MQTT "));
  if (!cfg.mqttEnabled) lcd.print(TR("AUS","OFF"));
  else lcd.print(mqttClient.connected() ? F("OK") : F("--"));

  lcdCursor(52, 38);
  lcd.print(F("AP "));
  lcd.print(apMode ? TR("AN","ON") : TR("AUS","OFF"));
}

void drawDisplayPageClimate(){
  lcd.setTextSize(1);
  lcdCursor(0,0);
  lcd.print(TR("KLIMA AHT10","CLIMATE AHT10"));

  if(!ahtOk){
    lcdCursor(0,14);
    lcd.print(TR("Sensor --","Sensor --"));
    lcdCursor(0,27);
    lcd.print(F("I2C 0x38"));
    lcdCursor(0,40);
    lcd.print(TR("Recovery aktiv","Recovery active"));
    return;
  }

  lcdCursor(0,13);
  lcd.print(TR("TEMP ","TEMP "));
  lcd.setTextSize(2);
  lcd.print(ahtTemperatureC,1);
  lcd.setTextSize(1);
  lcd.print(F("C"));

  lcdCursor(0,29);
  lcd.print(F("RH   "));
  lcd.print(ahtHumidityPercent,1);
  lcd.print(F("%"));

  lcdCursor(0,41);
  lcd.print(TR("TAU ","DEW "));
  lcd.print(ahtDewPointC,1);
  lcd.print(F("C "));

  lcd.print(F("R"));
  lcd.print(ahtCondensationReserveC,0);
}

void drawDisplayPageSystem() {
  // Page 4: compact health display.
  lcd.setTextSize(1);
  lcdCursor(0, 0);
  lcd.print(TR("SYSTEM ","SYSTEM "));
  lcd.print(FW_VERSION);

  lcdCursor(0, 12);
  lcd.print(F("HEAP "));
  lcd.print(ESP.getFreeHeap()/1024);
  lcd.print(F("k"));

  lcdCursor(44, 12);
  lcd.print(F("MIN "));
  lcd.print(minFreeHeapSeen/1024);
  lcd.print(F("k"));

  lcdCursor(0, 24);
  lcd.print(F("UP "));
  uint32_t mins=millis()/60000UL;
  if(mins<1440){
    lcd.print(mins);
    lcd.print(F("m"));
  } else {
    lcd.print(mins/1440UL);
    lcd.print(F("d"));
  }

  lcdCursor(44, 24);
  lcd.print(F("OTA "));
  lcd.print(F("WEB"));

  lcdCursor(0, 37);
  lcd.print(F("W "));
  lcd.print(WiFi.status()==WL_CONNECTED ? F("OK") : F("--"));
  lcd.print(F(" M "));
  lcd.print(cfg.mqttEnabled ? (mqttClient.connected()?F("OK"):F("--")) : TR("AUS","OFF"));

  lcdCursor(58, 37);
  lcd.print(F("S "));
  lcd.print(sensorOk ? F("OK") : F("--"));
}

void drawDisplayContent() {
  lcd.setTextColor(BLACK);
  lcd.setTextSize(1);

  if (!sensorOk) {
    lcdCursor(0, 0);
    lcd.print(TR("SENSOR FEHLER","SENSOR ERROR"));
    lcdCursor(0, 12);
    lcd.print(F("I2C 0x29"));
    lcdCursor(0, 24);
    lcd.print(TR("Recovery aktiv","Recovery active"));
    lcdCursor(0, 38);
    if (WiFi.status() == WL_CONNECTED) lcd.print(WiFi.localIP().toString());
    else if (apMode) lcd.print(WiFi.softAPIP().toString());
    return;
  }

  switch (displayPage) {
    case 0: drawDisplayPageMain(); break;
    case 1: drawDisplayPageSensor(); break;
    case 2: drawDisplayPageNetwork(); break;
    case 3: drawDisplayPageSystem(); break;
    default: drawDisplayPageClimate(); break;
  }
}

void drawDisplay() {
  lcd.clearDisplay();

  // Normal: one pass.
  lcdTextXOffset = 0;
  lcdTextYOffset = 0;
  drawDisplayContent();

  // Bold: additional horizontal pass.
  if (cfg.displayFontWeight >= 1) {
    lcdTextXOffset = 1;
    lcdTextYOffset = 0;
    drawDisplayContent();
  }

  // Extra bold: additional vertical pass.
  if (cfg.displayFontWeight >= 2) {
    lcdTextXOffset = 0;
    lcdTextYOffset = 1;
    drawDisplayContent();
  }

  lcdTextXOffset = 0;
  lcdTextYOffset = 0;
  lcd.display();
}

// -----------------------------------------------------------------------------
// MQTT
