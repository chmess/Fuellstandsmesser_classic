#include "AppRuntime.h"
#include "AppConstants.h"
#include "AppTypes.h"
#include "Sensors.h"
#include "Measurement.h"
#include "Display.h"
#include "MqttDiagnostics.h"
#include "WifiManager.h"
#include "ConfigI2C.h"
#include "HistoryTypes.h"
#include "History.h"
#include "WebServerManager.h"

#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <PubSubClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <Adafruit_VL53L0X.h>
#include <Adafruit_VL53L1X.h>
#include <time.h>
#include <math.h>

extern Config cfg;
extern Adafruit_PCD8544 lcd;
extern Adafruit_VL53L0X vl53l0x;
extern Adafruit_VL53L1X vl53l1x;
extern ESP8266WebServer server;
extern DNSServer dnsServer;
extern WiFiClient wifiClient;
extern PubSubClient mqttClient;
extern bool apMode;
extern bool sensorOk;
extern uint8_t activeSensorType;
extern uint32_t lastMeasureMs;
extern uint32_t lastDisplayMs;
extern uint32_t lastDisplayPageMs;
extern uint8_t displayPage;
extern uint32_t lastWifiRetryMs;
extern uint32_t wifiLostSinceMs;
extern uint32_t wifiReconnectCount;
extern uint32_t wifiReconnectErrors;
extern uint32_t minFreeHeapSeen;
extern bool webOtaActive;
extern bool webOtaSuccess;
extern uint32_t webOtaBytes;
extern String webOtaError;
extern uint32_t lastMqttRetryMs;
extern uint32_t lastTofRetryMs;
extern bool fsMounted;
extern FSInfo fsInfoCache;
extern uint16_t rawDistanceMm;
extern uint16_t filteredDistanceMm;
extern float tankHeightNowMm;
extern float tankPercent;
extern float tankLiters;
extern uint32_t measurementCount;
extern uint32_t measurementErrors;
extern uint32_t sensorRecoveries;
extern bool ahtOk;
extern bool ahtInitialized;
extern float ahtTemperatureC;
extern float ahtHumidityPercent;
extern float ahtDewPointC;
extern uint32_t ahtReadCount;
extern uint32_t ahtErrorCount;
extern uint32_t ahtRecoveryCount;
extern uint32_t ahtProbeCount;
extern uint8_t ahtConsecutiveErrors;
extern uint32_t lastAhtReadMs;
extern uint32_t lastAhtRetryMs;
extern uint32_t lastAhtValidMs;
extern uint32_t lastAhtRecoveryMs;
extern float ahtCondensationReserveC;
extern uint32_t cliCommandCount;
extern uint32_t cliErrorCount;
extern bool cliWifiDirty;
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
extern uint32_t mqttRetryIntervalMs;
extern uint32_t mqttPublishErrors;
extern char cliLine[];
extern size_t cliLineLen;
extern bool lowHeapActive;
extern uint32_t webLowHeapEvents;
extern bool otaInProgress;

uint8_t displayNextAutoPage(uint8_t current);
void handleSerialCli();
void cliExecute(char* line);

void cliPrintPrompt(){ Serial.print(F("fuell> ")); }

void cliPrintHelp(){
  Serial.println();
  Serial.println(F("WLAN CLI - Befehle"));
  Serial.println(F("  help"));
  Serial.println(F("  wifi status"));
  Serial.println(F("  wifi scan"));
  Serial.println(F("  wifi ssid <name>"));
  Serial.println(F("  wifi pass <passwort>"));
  Serial.println(F("  wifi save"));
  Serial.println(F("  wifi connect"));
  Serial.println(F("  wifi disconnect"));
  Serial.println(F("  wifi ap"));
  Serial.println(F("  wifi clear"));
  Serial.println(F("  mqtt status"));
  Serial.println(F("  mqtt test"));
  Serial.println(F("  mqtt reconnect"));
  Serial.println();
  Serial.println(F("Hinweis: SSID/Passwort werden erst mit 'wifi save'"));
  Serial.println(F("dauerhaft in EEPROM gespeichert."));
}

void cliPrintWifiStatus(){
  Serial.println(F("[CLI] WLAN STATUS"));
  Serial.print(F("  SSID config : "));
  if(cfg.wifiSsid[0])Serial.println(cfg.wifiSsid); else Serial.println(F("<leer>"));
  Serial.print(F("  Passwort    : "));
  Serial.println(cfg.wifiPass[0]?F("gesetzt"):F("<leer>"));
  Serial.print(F("  Dirty       : "));
  Serial.println(cliWifiDirty?F("JA - noch nicht gespeichert"):F("NEIN"));
  Serial.print(F("  STA         : "));
  if(WiFi.status()==WL_CONNECTED){
    Serial.print(F("VERBUNDEN ")); Serial.println(WiFi.SSID());
    Serial.print(F("  IP          : ")); Serial.println(WiFi.localIP());
    Serial.print(F("  RSSI        : ")); Serial.print(WiFi.RSSI()); Serial.println(F(" dBm"));
  }else Serial.println(F("GETRENNT"));
  Serial.print(F("  AP          : "));
  if(apMode){ Serial.print(F("AN ")); Serial.println(WiFi.softAPIP()); } else Serial.println(F("AUS"));
  Serial.print(F("  MQTT        : "));
  if(!cfg.mqttEnabled)Serial.println(F("AUS")); else Serial.println(mqttClient.connected()?F("VERBUNDEN"):F("GETRENNT"));
}

void cliWifiScan(){
  Serial.println(F("[CLI] WLAN Scan gestartet ..."));
  WiFiMode_t oldMode=WiFi.getMode();
  if(oldMode==WIFI_OFF)WiFi.mode(WIFI_STA);
  int n=WiFi.scanNetworks(false,true);
  if(n<0){ Serial.println(F("[CLI] Scan FEHLER")); cliErrorCount++; return; }
  Serial.print(F("[CLI] gefunden: ")); Serial.println(n);
  if(n==0) Serial.println(F("  keine WLANs gefunden"));
  else{
    for(int i=0;i<n;i++){
      Serial.print(F("  ")); Serial.print(i+1); Serial.print(F(". "));
      Serial.print(WiFi.SSID(i)); Serial.print(F(" | ")); Serial.print(WiFi.RSSI(i));
      Serial.print(F(" dBm | CH ")); Serial.print(WiFi.channel(i)); Serial.print(F(" | "));
      Serial.println(WiFi.encryptionType(i)==ENC_TYPE_NONE?F("OFFEN"):F("GESCHUETZT"));
      yield();
    }
  }
  WiFi.scanDelete();
}

void cliStartConfiguredWifi(){
  Serial.println(F("[CLI] WLAN Verbindung mit aktueller RAM-Konfiguration"));
  if(apMode){ dnsServer.stop(); WiFi.softAPdisconnect(true); apMode=false; delay(20); }
  mqttClient.disconnect(); wifiLostSinceMs=0; lastWifiRetryMs=0;
  connectWifi();
  if(WiFi.status()==WL_CONNECTED){
    historySetupTime();
    if(cfg.mqttEnabled){ mqttClient.setBufferSize(MQTT_BUFFER_NORMAL); connectMqtt(); }
    Serial.println(F("[CLI] WLAN Verbindung abgeschlossen"));
  }else Serial.println(F("[CLI] Verbindung fehlgeschlagen - Fallback-AP aktiv"));
}

void cliExecute(char* line){
  if(!line)return;
  while(*line==' ' || *line=='\t')line++;
  size_t len=strlen(line);
  while(len>0 && (line[len-1]==' ' || line[len-1]=='\t')) line[--len]='\0';
  if(len==0)return;
  cliCommandCount++;

  if(!strcasecmp(line,"help") || !strcmp(line,"?")){ cliPrintHelp(); return; }
  if(!strcasecmp(line,"wifi status")){ cliPrintWifiStatus(); return; }
  if(!strcasecmp(line,"wifi scan")){ cliWifiScan(); return; }

  if(!strncasecmp(line,"wifi ssid ",10)){
    const char* value=line+10; while(*value==' ')value++;
    if(!*value){ Serial.println(F("[CLI] Fehler: SSID leer")); cliErrorCount++; return; }
    if(strlen(value)>=sizeof(cfg.wifiSsid)){ Serial.println(F("[CLI] Fehler: SSID zu lang")); cliErrorCount++; return; }
    strlcpy(cfg.wifiSsid,value,sizeof(cfg.wifiSsid)); cliWifiDirty=true;
    Serial.print(F("[CLI] SSID gesetzt: ")); Serial.println(cfg.wifiSsid);
    Serial.println(F("[CLI] Noch nicht gespeichert")); return;
  }

  if(!strncasecmp(line,"wifi pass ",10)){
    const char* value=line+10; while(*value==' ')value++;
    if(strlen(value)>=sizeof(cfg.wifiPass)){ Serial.println(F("[CLI] Fehler: Passwort zu lang")); cliErrorCount++; return; }
    strlcpy(cfg.wifiPass,value,sizeof(cfg.wifiPass)); cliWifiDirty=true;
    Serial.print(F("[CLI] Passwort gesetzt (")); Serial.print(strlen(cfg.wifiPass)); Serial.println(F(" Zeichen)"));
    Serial.println(F("[CLI] Passwort wird nicht ausgegeben")); Serial.println(F("[CLI] Noch nicht gespeichert")); return;
  }

  if(!strcasecmp(line,"wifi save")){ validateConfig(true); saveConfig(); cliWifiDirty=false; Serial.println(F("[CLI] WLAN-Konfiguration gespeichert")); return; }
  if(!strcasecmp(line,"wifi connect")){ cliStartConfiguredWifi(); return; }
  if(!strcasecmp(line,"wifi disconnect")){ mqttClient.disconnect(); WiFi.disconnect(); delay(20); startAp(); Serial.println(F("[CLI] STA getrennt - Fallback-AP aktiv")); return; }
  if(!strcasecmp(line,"wifi ap")){ startAp(); Serial.println(F("[CLI] Fallback-AP aktiv")); return; }

  if(!strcasecmp(line,"wifi clear")){
    cfg.wifiSsid[0]='\0'; cfg.wifiPass[0]='\0'; validateConfig(true); saveConfig(); cliWifiDirty=false;
    mqttClient.disconnect(); WiFi.disconnect(); delay(20); startAp();
    Serial.println(F("[CLI] WLAN Zugangsdaten geloescht")); Serial.println(F("[CLI] Fallback-AP aktiv")); return;
  }

  if(!strcasecmp(line,"mqtt status")){ mqttPrintStatus(); return; }
  if(!strcasecmp(line,"mqtt test")){ mqttDiagnosticTest(); return; }
  if(!strcasecmp(line,"mqtt reconnect")){ mqttClient.disconnect(); mqttRetryIntervalMs=MQTT_RETRY_MS; lastMqttRetryMs=0; connectMqtt(); return; }

  Serial.print(F("[CLI] Unbekannter Befehl: ")); Serial.println(line);
  Serial.println(F("[CLI] 'help' fuer Befehlsliste")); cliErrorCount++;
}

void handleSerialCli(){
  while(Serial.available()>0){
    const int c=Serial.read(); if(c<0)break;
    if(c=='\r')continue;
    if(c=='\n'){
      if(cliLineLen>0){ cliLine[cliLineLen]='\0'; Serial.println(); cliExecute(cliLine); cliLineLen=0; cliPrintPrompt(); }
      continue;
    }
    if(c==8 || c==127){ if(cliLineLen>0)cliLineLen--; continue; }
    if(c>=32 && c<=126){
      if(cliLineLen<CLI_LINE_MAX-1) cliLine[cliLineLen++]=(char)c;
      else{ cliLineLen=0; cliErrorCount++; Serial.println(); Serial.println(F("[CLI] Eingabe zu lang - verworfen")); cliPrintPrompt(); }
    }
  }
}

void setup() {
  Serial.begin(115200); delay(100);
  Serial.println();
  Serial.println(F("============================================================"));
  Serial.print(F(" FUELLSTANDSMESSER_CLASSIC ")); Serial.println(FW_VERSION);
  Serial.println(F(" ESP8266 D1 mini"));
  Serial.println(F("============================================================"));
  Serial.println(F("[CLI] WLAN-CLI aktiv | 'help' eingeben"));
  printBootDiagnostics();

  EEPROM.begin(sizeof(Config) + 32);
  loadConfig();
  if (validateConfig(true)) { Serial.println(F("[CONFIG] korrigierte Werte werden gespeichert")); saveConfig(); }
  else { Serial.println(F("[CONFIG] Plausibilitaet OK")); cfg.haDiscoveryEnabled = false; }

  setupFilesystem(); historySetupAfterFilesystem();
  Serial.println(F("[MQTT] Kompatibilitaet: average=Liter, fuellhoehe=gefilterter Sensorabstand mm"));

  Serial.print(F("[FILTER] min=")); Serial.print(cfg.minDistanceMm);
  Serial.print(F(" max=")); Serial.print(cfg.maxDistanceMm);
  Serial.print(F(" maxJump=")); Serial.print(cfg.maxJumpMm);
  Serial.print(F(" mm confirm=")); Serial.print(JUMP_CONFIRM_COUNT);
  Serial.print(F("x tolerance=")); Serial.print(JUMP_CONFIRM_TOLERANCE_MM, 0); Serial.println(F(" mm"));

  lcd.begin();
  Serial.print(F("[LCD] Nokia 5110 OK | 5 Seiten | Auto=")); Serial.print(cfg.displayAutoRotate?F("AN"):F("AUS"));
  Serial.print(F(" | Wechsel ")); Serial.print(cfg.displayPageSeconds); Serial.println(F("s"));
  Serial.print(F("[LCD] Invert=")); Serial.println(cfg.displayInvert?F("AN"):F("AUS"));
  Serial.print(F("[LCD] Auto-Seitenmaske=0x")); Serial.println(cfg.displayPageMask, HEX);
  Serial.print(F("[LCD] Schrift="));
  if(cfg.displayFontWeight==0) Serial.println(F("NORMAL")); else if(cfg.displayFontWeight==1) Serial.println(F("FETT")); else Serial.println(F("EXTRA-FETT"));
  Serial.println(F("[LCD] Layout=LESBAR V2"));
  lcd.setContrast(cfg.displayContrast); lcd.invertDisplay(cfg.displayInvert); lcd.clearDisplay(); lcd.setTextColor(BLACK); lcd.setTextSize(1);
  lcd.setCursor(0, 0); lcd.println(F("Fuellstand")); lcd.println(F("Classic")); lcd.println(FW_VERSION); lcd.display();

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL); Wire.setClock(100000); delay(20); scanI2C();
  Serial.print(F("[I2C] SDA=D2 GPIO")); Serial.print(PIN_I2C_SDA); Serial.print(F(" SCL=D1 GPIO")); Serial.println(PIN_I2C_SCL);

  sensorOk = initToF(); ahtOk = initAht10();
  if(!cfg.ahtEnabled) Serial.println(F("[AHT10] deaktiviert"));
  else if(!ahtOk) Serial.println(F("[AHT10] nicht erkannt - Betrieb ohne Klimasensor"));

  connectWifi(); historySetupTime(); setupWeb();

  if (cfg.mqttEnabled) { mqttClient.setBufferSize(MQTT_BUFFER_NORMAL); connectMqtt(); }

  lastMeasureMs = millis() - cfg.measurementIntervalMs;
  lastAhtReadMs = millis() - cfg.ahtIntervalMs;
  Serial.println(); cliPrintPrompt();
}

void loop() {
  uint32_t now = millis();
  handleSerialCli(); server.handleClient(); if (apMode) dnsServer.processNextRequest(); updateHeapDiag();

  static uint32_t lowHeapLogMs = 0;
  static uint32_t healthLogMs = 0;
  if(millis() - healthLogMs > 60000UL){
    healthLogMs = millis();
    const uint32_t healthHeap=ESP.getFreeHeap(); const uint32_t healthBlock=ESP.getMaxFreeBlockSize();
    Serial.print(F("[HEALTH] "));
    if(healthHeap<6144UL || healthBlock<3072UL)Serial.print(F("CRIT "));
    else if(healthHeap<9216UL || healthBlock<4096UL)Serial.print(F("WARN "));
    else Serial.print(F("OK "));
    Serial.print(F("heap=")); Serial.print(healthHeap); Serial.print(F(" min=")); Serial.print(minFreeHeapSeen);
    Serial.print(F(" block=")); Serial.print(healthBlock); Serial.print(F(" frag=")); Serial.print(ESP.getHeapFragmentation());
    Serial.print(F("% histErr=")); Serial.print(historyApiErrors); Serial.print(F(" mqttErr=")); Serial.println(mqttPublishErrors);
  }

  const uint32_t heapNow=ESP.getFreeHeap(); const uint32_t maxBlockNow=ESP.getMaxFreeBlockSize(); const uint8_t fragNow=ESP.getHeapFragmentation();
  const bool heapCritical=(heapNow<6144UL || maxBlockNow<3072UL);
  const bool heapWarning =(heapNow<9216UL || maxBlockNow<4096UL);

  if(heapCritical){
    if(!lowHeapActive){
      lowHeapActive=true; webLowHeapEvents++;
      Serial.print(F("[HEAP] LOW event #")); Serial.print(webLowHeapEvents);
      Serial.print(F(" free=")); Serial.print(heapNow); Serial.print(F(" maxBlock=")); Serial.print(maxBlockNow);
      Serial.print(F(" frag=")); Serial.print(fragNow); Serial.println('%');
    }
  }else if(heapNow>=10240UL && maxBlockNow>=5120UL) lowHeapActive=false;

  const uint32_t heapLogInterval=heapCritical?10000UL:30000UL;
  if(heapWarning && millis()-lowHeapLogMs>=heapLogInterval){
    lowHeapLogMs=millis();
    Serial.print(heapCritical?F("[HEAP] CRIT free="):F("[HEAP] WARN free="));
    Serial.print(heapNow); Serial.print(F(" maxBlock=")); Serial.print(maxBlockNow);
    Serial.print(F(" frag=")); Serial.print(fragNow); Serial.println('%');
  }

  if(!webOtaActive) historyLoop();
  if (otaInProgress) { yield(); return; }

  if (WiFi.status() == WL_CONNECTED) {
    if (wifiLostSinceMs != 0) {
      wifiReconnectCount++; Serial.print(F("[WIFI] wieder verbunden IP=")); Serial.print(WiFi.localIP());
      Serial.print(F(" reconnects=")); Serial.println(wifiReconnectCount); wifiLostSinceMs = 0;
    }
  } else if (!apMode && strlen(cfg.wifiSsid) > 0) {
    if (wifiLostSinceMs == 0) { wifiLostSinceMs = now; Serial.println(F("[WIFI] Verbindung verloren")); }
    if (now - lastWifiRetryMs >= WIFI_RETRY_MS) {
      lastWifiRetryMs = now; wifiReconnectErrors++; Serial.print(F("[WIFI] Reconnect-Versuch ")); Serial.println(wifiReconnectErrors);
      WiFi.disconnect(); delay(10); WiFi.begin(cfg.wifiSsid, cfg.wifiPass);
    }
    if (now - wifiLostSinceMs >= WIFI_AP_FALLBACK_MS) { Serial.println(F("[WIFI] 120s offline -> Fallback-AP")); startAp(); }
  }

  if (cfg.mqttEnabled && WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected() && now - lastMqttRetryMs >= mqttRetryIntervalMs) { lastMqttRetryMs = now; connectMqtt(); }
    if(!webOtaActive) mqttClient.loop();
  }

  if (now - lastMeasureMs >= cfg.measurementIntervalMs) { lastMeasureMs = now; performMeasurement(); publishMqtt(); }

  if(cfg.ahtEnabled){
    if(ahtInitialized){
      if(now-lastAhtReadMs>=cfg.ahtIntervalMs){ lastAhtReadMs=now; const bool wasOk=ahtOk; if(readAht10() && !wasOk && cfg.mqttEnabled && mqttClient.connected()){} }
    }else if(now-lastAhtRetryMs>=AHT_RETRY_MS){
      lastAhtRetryMs=now;
      if(initAht10()){ lastAhtReadMs=now; if(readAht10() && cfg.mqttEnabled && mqttClient.connected()){} }
      else if((ahtProbeCount%12U)==0U){ Serial.print(F("[AHT10] weiterhin nicht erreichbar @ 0x38 probes=")); Serial.println(ahtProbeCount); }
    }
  }else{ ahtInitialized=false; ahtOk=false; }

  if (cfg.displayAutoRotate && now - lastDisplayPageMs >= (uint32_t)cfg.displayPageSeconds * 1000UL) {
    lastDisplayPageMs = now; displayPage = displayNextAutoPage(displayPage); drawDisplay();
  }
  if (now - lastDisplayMs >= DISPLAY_MS) { lastDisplayMs = now; drawDisplay(); }
  yield();
}
