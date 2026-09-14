#include "WebServerManager.h"
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

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <Updater.h>
#include <LittleFS.h>
#include <FS.h>
#include <PubSubClient.h>
#include <time.h>
#include <math.h>


// Constants that were file-local in the original main sketch.
// Repeated here with identical values for this translation unit.


// Shared application state still owned by the main sketch.
extern Config cfg;

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

extern bool fsMounted;
extern FSInfo fsInfoCache;

extern uint32_t webRequestCount;
extern uint32_t webLowHeapEvents;
extern bool lowHeapActive;
extern uint32_t webOtaAttempts;
extern uint32_t webOtaSuccessCount;
extern uint32_t webOtaErrorCount;
extern uint32_t lowestMaxBlockSeen;
extern uint8_t highestHeapFragSeen;
extern bool otaInProgress;
extern uint8_t otaLastPercent;

extern uint32_t lastMqttRetryMs;
extern uint32_t lastTofRetryMs;

extern uint32_t mqttConnectCount;
extern uint32_t mqttConnectErrors;
extern uint32_t mqttRetryIntervalMs;
extern uint32_t mqttLastConnectDurationMs;
extern IPAddress mqttLastResolvedIp;
extern bool mqttLastDnsOk;
extern bool mqttLastTcpOk;
extern uint32_t mqttPublishCount;
extern uint32_t mqttPublishErrors;

extern float rawDistanceMm;
extern float filteredDistanceMm;
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
extern bool startupMeasurementStable;
extern uint8_t startupCandidateCount;
extern bool cliWifiDirty;

// Helpers implemented in the remaining setup/loop/CLI sketch.
void printSystemStatus();

// -----------------------------------------------------------------------------
void noteWebRequest() {
  webRequestCount++;
  updateHeapDiag();
}

void webPrepareConnectionClose(){
  // ESP8266: Browser-KeepAlive kann mehrere TCP-Puffer gleichzeitig halten.
  // Fuer dieses kleine Geraet ist "Connection: close" RAM-schonender.
  server.sendHeader("Connection","close");
}

void webFinishConnection(){
  // Nach komplett gesendeter Antwort Socket explizit freigeben.
  // Ein kurzes yield gibt lwIP Gelegenheit, ACK/FIN abzuarbeiten.
  yield();
  WiFiClient c=server.client();
  if(c && c.connected()){
    c.stop();
  }
}

void webSendSafe(const String& value) {
  // WICHTIG bei ESP8266 chunked transfer:
  // sendContent("") sendet den 0-Byte-Endchunk und beendet die HTTP-Antwort.
  // Leere Konfigurationswerte duerfen deshalb NICHT direkt an sendContent().
  if (value.length() > 0) {
    server.sendContent(value);
  }
}

void webSendSafe(const char* value) {
  if (value && value[0] != '\0') {
    server.sendContent(value);
  }
}

void webSendUInt(uint32_t value){
  char b[16];
  ultoa(value,b,10);
  webSendSafe(b);
}

void webSendInt(int32_t value){
  char b[16];
  ltoa(value,b,10);
  webSendSafe(b);
}

void webSendFloat(float value,uint8_t decimals){
  char b[24];
  dtostrf(value,0,decimals,b);
  char* p=b;
  while(*p==' ')p++;
  webSendSafe(p);
}

void webStreamBegin(const __FlashStringHelper* title) {
  noteWebRequest();
  webPrepareConnectionClose();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  server.sendContent(F("<!DOCTYPE html><html lang='de'><head><meta charset='UTF-8'>"
                       "<meta name='viewport' content='width=device-width,initial-scale=1'><title>"));
  server.sendContent(title);
  server.sendContent(F("</title><style>"
                       "body{font-family:Arial,sans-serif;background:#111;color:#eee;margin:0;padding:20px}"
                       ".wrap{max-width:1000px;margin:auto}.card,.tile{background:#1d1d1d;padding:16px;margin-bottom:16px;border-radius:12px;box-shadow:0 2px 8px #000}"
                       "h1,h2{color:#4dd0e1}button,.btn{display:inline-block;padding:10px 14px;background:#008c9e;color:#fff;border:0;border-radius:6px;text-decoration:none;cursor:pointer;margin:3px}"
                       ".danger{background:#b3261e!important}.muted{color:#999}.ok{color:#65e572}.bad{color:#ff6565}"
                       ".nav{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px;margin-bottom:16px}"
                       ".nav a{text-align:center;padding:10px 6px;background:#292929;border:1px solid #444;border-radius:8px;color:#eee;text-decoration:none;font-weight:700}"
                       ".nav a.active{background:#008c9e;border-color:#008c9e}"
                       ".grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}"
                       ".metric,.metric-card{background:#292929;border-radius:10px;padding:10px}"
                       ".metric b,.metric-card div{display:block;font-size:1.1rem;margin-top:3px;font-weight:700}"
                       "table{width:100%;border-collapse:collapse}td,th{padding:7px;border-bottom:1px solid #444;text-align:left}"
                       "input,select{width:100%;box-sizing:border-box;padding:9px;margin:4px 0 10px;background:#2a2a2a;color:#fff;border:1px solid #555;border-radius:6px}"
                       ".topbar{display:flex;justify-content:space-between;align-items:center;gap:10px;flex-wrap:wrap}.links{display:flex;gap:6px;flex-wrap:wrap}"
                       "@media(max-width:700px){.grid{grid-template-columns:1fr}.nav{grid-template-columns:repeat(2,1fr)}}"
                       "</style></head><body><div class='wrap'>"));
}

void webStreamNav(uint8_t active) {
  server.sendContent(F("<div class='nav'>"));
  server.sendContent(active==0?F("<a class='active' href='/'>Dashboard</a>"):F("<a href='/'>Dashboard</a>"));
  server.sendContent(active==1?F("<a class='active' href='/history'>Historie</a>"):F("<a href='/history'>Historie</a>"));
  server.sendContent(active==2?F("<a class='active' href='/settings'>Einstellungen</a>"):F("<a href='/settings'>Einstellungen</a>"));
  server.sendContent(active==3?F("<a class='active' href='/systemstatus'>System</a>"):F("<a href='/systemstatus'>System</a>"));
  server.sendContent(F("</div>"));
}

void webStreamEnd() {
  server.sendContent(F("<div style='color:#777;text-align:center;padding:8px 0'>Fuellstandsmesser_classic "));
  webSendSafe(FW_VERSION);
  server.sendContent(F("</div></div></body></html>"));
  server.sendContent("");
  webFinishConnection();
}


void historyBuildStatsCache() {
  // Maximal einmal pro 60 s neu berechnen. Nach Schreibvorgaengen wird
  // der Cache explizit invalidiert.
  if (historyStatsCache.valid &&
      (uint32_t)(millis() - historyStatsCache.builtMs) < 60000UL) {
    return;
  }

  const uint32_t t0=millis();

  historyStatsCache.c1 = 0.0f;
  historyStatsCache.c7 = 0.0f;
  historyStatsCache.c30 = 0.0f;
  historyStatsCache.c365 = 0.0f;

  if (!historyReady || historyHeader.count == 0) {
    historyStatsCache.valid = true;
    historyStatsCache.builtMs = millis();
    return;
  }

  File f = LittleFS.open(HISTORY_FILE, "r");
  if (!f) return;

  // Zeitanker nur EINMAL bestimmen.
  uint32_t anchorDay=0;
  bool anchorFromClock=historyDateNow(anchorDay);

  DailyHistoryRecord newest;
  uint32_t newestLogical=0;
  const bool haveNewest=historyNewestRecordFromOpenFile(f,newest,newestLogical);
