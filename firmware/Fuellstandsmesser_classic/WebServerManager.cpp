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

  if(!anchorFromClock){
    if(haveNewest)anchorDay=newest.dayKey;
  }else if(haveNewest && newest.dayKey>anchorDay){
    // Schutz gegen veraltete/falsche Uhrzeit.
    anchorDay=newest.dayKey;
    anchorFromClock=false;
  }

  if(anchorDay==0){
    f.close();
    historyStatsCache.valid=true;
    historyStatsCache.builtMs=millis();
    return;
  }

  const uint32_t first1   = historyFirstDayForAnchor(anchorDay,1);
  const uint32_t first7   = historyFirstDayForAnchor(anchorDay,7);
  const uint32_t first30  = historyFirstDayForAnchor(anchorDay,30);
  const uint32_t first365 = historyFirstDayForAnchor(anchorDay,365);

  const uint32_t oldest=historyOldestPhysicalIndex();
  uint32_t scanned=0,eligible=0,uniqueDays=0,duplicatesSkipped=0;
  uint32_t lastDaySeen=0;
  bool haveLastDaySeen=false;

  if(haveNewest){
    for(int32_t li=(int32_t)newestLogical;li>=0;--li){
      const uint32_t physical=(oldest+(uint32_t)li)%historyHeader.capacity;
      DailyHistoryRecord r;
      scanned++;

      if(!historyReadRecordFromOpenFile(f,physical,r)){
        if((scanned&0x3F)==0)yield();
        continue;
      }

      if(r.dayKey>anchorDay)continue;
      if(first365 && r.dayKey<first365)break;

      eligible++;

      if(haveLastDaySeen && r.dayKey==lastDaySeen){
        duplicatesSkipped++;
        if((scanned&0x3F)==0)yield();
        continue;
      }

      lastDaySeen=r.dayKey;
      haveLastDaySeen=true;
      uniqueDays++;

      const float c=(float)r.consumptionLiters;

      historyStatsCache.c365 += c;
      if(!first30 || r.dayKey>=first30) historyStatsCache.c30 += c;
      if(!first7  || r.dayKey>=first7)  historyStatsCache.c7 += c;
      if(!first1  || r.dayKey>=first1)  historyStatsCache.c1 += c;

      if((scanned&0x3F)==0)yield();
    }
  }

  f.close();

  historyStatsCache.valid = true;
  historyStatsCache.builtMs = millis();

  Serial.print(F("[HISTORY STATS] anchor="));
  Serial.print(anchorDay);
  Serial.print(anchorFromClock?F("(clock)"):F("(history)"));
  Serial.print(F(" scanned="));
  Serial.print(scanned);
  Serial.print(F(" eligible="));
  Serial.print(eligible);
  Serial.print(F(" unique="));
  Serial.print(uniqueDays);
  Serial.print(F(" dupSkip="));
  Serial.print(duplicatesSkipped);
  Serial.print(F(" time="));
  Serial.print(millis()-t0);
  Serial.println(F(" ms"));
}


float historyConsumptionDays(uint16_t days) {
  historyBuildStatsCache();
  if (days <= 1) return historyStatsCache.c1;
  if (days <= 7) return historyStatsCache.c7;
  if (days <= 30) return historyStatsCache.c30;
  return historyStatsCache.c365;
}

void handleRoot() {
  const uint32_t rootHeapBefore=ESP.getFreeHeap();
  Serial.print(F("[WEB ROOT] start heap="));
  Serial.print(rootHeapBefore);
  Serial.print(F(" maxBlock="));
  Serial.println(ESP.getMaxFreeBlockSize());

  webStreamBegin(F("Fuellstandsmesser_classic"));
  webStreamNav(0);

  server.sendContent(F(
    "<style>"
    ".dash{display:grid;grid-template-columns:repeat(12,1fr);gap:12px}.hero{grid-column:span 7}.side{grid-column:span 5}.wide{grid-column:span 12}"
    ".heroGrid{display:grid;grid-template-columns:minmax(260px,1.3fr) minmax(180px,.9fr);gap:14px;align-items:center}.heroInfo{min-width:0}"
    ".tankBox{display:flex;justify-content:center;align-items:center;background:linear-gradient(180deg,#1a1f26,#12161b);border:1px solid #2f3943;border-radius:16px;padding:10px;min-height:290px}"
    ".tankSvg{width:100%;max-width:260px;height:auto;display:block}.tankLabel{font-size:12px;fill:#d8e3eb}.tankSmall{font-size:10px;fill:#91a4b5}.tankValue{font-size:15px;font-weight:700;fill:#ffffff}.tankTitle{font-size:14px;font-weight:700;fill:#dbe7ef}"
    ".big{font-size:3rem;font-weight:800}.bar{height:16px;background:#333;border-radius:9px;overflow:hidden;margin:9px 0}.barFill{height:100%;width:0}"
    ".statusGrid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}.status{background:#292929;border-radius:10px;padding:9px}"
  ));
  server.sendContent(F(
    ".dot{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:5px}.green{background:#42d65b}.red{background:#ff4d4d}.gray{background:#888}"
    ".chartWrap{position:relative;height:260px}.chart{width:100%;height:100%}.chartTip{position:absolute;display:none;pointer-events:none;min-width:170px;background:#101418;border:1px solid #4d5965;border-radius:9px;padding:8px;box-shadow:0 4px 14px #000;font-size:12px;z-index:5}.legend{display:flex;gap:14px;flex-wrap:wrap;color:#aaa;font-size:.82rem;margin-top:8px}.legend i{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:4px}.periods{display:flex;gap:5px;flex-wrap:wrap}.periodBtn{background:#292929;border:1px solid #555;border-radius:999px;padding:6px 10px;color:#eee}.periodBtn.active{background:#1769aa}.chartToggles{display:flex;gap:10px;align-items:center;flex-wrap:wrap;font-size:.82rem;color:#bbb}.chartToggles label{display:flex;align-items:center;gap:4px}.chartToggles input{width:auto;margin:0}"
    "@media(max-width:760px){.hero,.side,.wide{grid-column:span 12}.heroGrid{grid-template-columns:1fr}.statusGrid{grid-template-columns:1fr}.chartWrap{height:220px}}"
    ".chartToggles{margin-left:auto;padding:2px 0}.chartToggles label{padding:5px 8px;border:1px solid #3b3b3b;border-radius:999px;background:#202020;cursor:pointer}.chartToggles label.off{opacity:.38;cursor:not-allowed}.chartToggles input:disabled{cursor:not-allowed}.periods{display:flex;gap:6px;flex-wrap:wrap}.periodBtn,.monthYearsBtn{min-height:32px}.muted.compact{margin:5px 0 8px}</style><div class='dash'>"
  ));
  server.sendContent(F(
    "<div class='tile hero'><h2>Füllstand</h2><div class='heroGrid'><div class='heroInfo'><div><span id='liters' class='big'>--</span> L &nbsp; <b id='percent'>-- %</b></div>"
    "<div class='bar'><div id='bar' class='barFill'></div></div>"
    "<div class='grid'><div class='metric'>Füllhöhe<b id='height'>-- mm</b></div><div class='metric'>Sensorabstand<b id='distance'>-- mm</b></div>"
    "<div class='metric'>Tankfaktor<b id='lpm'>-- L/mm</b></div><div class='metric'>RSSI<b id='rssi'>-- dBm</b></div></div>"
    "<p id='updated' class='muted'>Warte auf Messdaten …</p></div>"
    "<div class='tankBox'><svg class='tankSvg' viewBox='0 0 240 280' xmlns='http://www.w3.org/2000/svg' aria-label='Heizöltank'>"
    "<defs>"
    "<linearGradient id='tankOilGrad' x1='0' x2='0' y1='0' y2='1'><stop id='oilTop' offset='0%' stop-color='#ffd35a'/><stop id='oilBottom' offset='100%' stop-color='#c97a00'/></linearGradient>"
  ));
  server.sendContent(F(
    "<linearGradient id='tankBodyGrad' x1='0' x2='0' y1='0' y2='1'><stop offset='0%' stop-color='#e1e7ec'/><stop offset='100%' stop-color='#98a5b1'/></linearGradient>"
    "<clipPath id='rectClip'><rect x='56' y='42' rx='22' ry='22' width='128' height='158'/></clipPath>"
    "<clipPath id='cylClip'><rect x='28' y='72' rx='56' ry='56' width='184' height='112'/></clipPath>"
    "</defs>"
    "<g id='tankRectGroup'>"
    "<rect x='44' y='28' rx='34' ry='34' width='152' height='184' fill='url(#tankBodyGrad)' stroke='#4c5964' stroke-width='3'/>"
    "<rect x='56' y='42' rx='22' ry='22' width='128' height='158' fill='#182129' stroke='#6d7b88' stroke-width='2'/>"
    "<rect id='oilFillRect' x='56' y='192' width='128' height='8' fill='url(#tankOilGrad)' clip-path='url(#rectClip)'/>"
    "<path id='oilWaveRect' d='M56 192 C70 188 84 196 98 192 C112 188 126 196 140 192 C154 188 168 196 184 192 L184 200 L56 200 Z' fill='#ffe07a' opacity='.86' clip-path='url(#rectClip)'/>"
  ));
  server.sendContent(F(
    "<line x1='56' y1='168.4' x2='184' y2='168.4' stroke='#ff6666' stroke-width='1.3' stroke-dasharray='5 4' opacity='.9'/>"
    "<text x='188' y='172' class='tankSmall' fill='#ff8f8f'>20%</text>"
    "<line x1='120' y1='14' x2='120' y2='28' stroke='#8895a2' stroke-width='4'/><rect x='108' y='6' width='24' height='10' rx='3' fill='#7f8d99'/>"
    "<rect x='62' y='216' width='20' height='18' rx='3' fill='#6f7d8a'/><rect x='158' y='216' width='20' height='18' rx='3' fill='#6f7d8a'/>"
    "<rect x='68' y='234' width='8' height='26' rx='2' fill='#6f7d8a'/><rect x='164' y='234' width='8' height='26' rx='2' fill='#6f7d8a'/>"
    "</g>"
    "<g id='tankCylGroup' style='display:none'>"
    "<rect x='18' y='62' rx='66' ry='66' width='204' height='132' fill='url(#tankBodyGrad)' stroke='#4c5964' stroke-width='3'/>"
    "<rect x='28' y='72' rx='56' ry='56' width='184' height='112' fill='#182129' stroke='#6d7b88' stroke-width='2'/>"
  ));
  server.sendContent(F(
    "<rect id='oilFillCyl' x='28' y='176' width='184' height='8' fill='url(#tankOilGrad)' clip-path='url(#cylClip)'/>"
    "<path id='oilWaveCyl' d='M28 176 C48 172 68 180 88 176 C108 172 128 180 148 176 C168 172 188 180 212 176 L212 184 L28 184 Z' fill='#ffe07a' opacity='.86' clip-path='url(#cylClip)'/>"
    "<line x1='28' y1='161.6' x2='212' y2='161.6' stroke='#ff6666' stroke-width='1.3' stroke-dasharray='5 4' opacity='.9'/>"
    "<text x='214' y='165' class='tankSmall' fill='#ff8f8f'>20%</text>"
    "<line x1='120' y1='48' x2='120' y2='62' stroke='#8895a2' stroke-width='4'/><rect x='108' y='40' width='24' height='10' rx='3' fill='#7f8d99'/>"
    "<rect x='54' y='194' width='22' height='12' rx='3' fill='#6f7d8a'/><rect x='164' y='194' width='22' height='12' rx='3' fill='#6f7d8a'/>"
    "</g>"
    "<text x='120' y='24' text-anchor='middle' class='tankTitle' id='tankShapeText'>Heizöltank</text>"
    "<rect x='68' y='88' width='104' height='78' rx='10' fill='#111820' opacity='.78'/>"
  ));
  server.sendContent(F(
    "<text x='120' y='110' text-anchor='middle' class='tankValue' id='tankPercentText'>-- %</text>"
    "<text x='120' y='126' text-anchor='middle' class='tankSmall'>Füllstand</text>"
    "<text x='78' y='145' class='tankLabel'>Liter</text><text x='162' y='145' text-anchor='end' class='tankValue' id='tankLitersText'>-- L</text>"
    "<text x='78' y='160' class='tankLabel'>Höhe</text><text x='162' y='160' text-anchor='end' class='tankValue' id='tankHeightText'>-- mm</text>"
    "<text x='78' y='175' class='tankLabel'>Distanz</text><text x='162' y='175' text-anchor='end' class='tankValue' id='tankDistanceText'>-- mm</text>"
    "<text x='120' y='270' text-anchor='middle' class='tankSmall'>Tankdarstellung · live</text>"
    "</svg></div></div></div>"
    "<div class='tile side'><h2>Verbrauch</h2><div class='grid'>"
    "<div class='metric'>Heute<b id='c1'>-- L</b></div><div class='metric'>7 Tage<b id='c7'>-- L</b></div>"
  ));
  server.sendContent(F(
    "<div class='metric'>30 Tage<b id='c30'>-- L</b></div><div class='metric'>365 Tage<b id='c365'>-- L</b></div></div>"
    "<h2>Status</h2><div class='statusGrid'><div class='status'><span id='dw' class='dot gray'></span>WLAN</div>"
    "<div class='status'><span id='dm' class='dot gray'></span>MQTT</div><div class='status'><span id='dt' class='dot gray'></span>ToF</div></div></div>"
    "<div class='tile wide'><div class='topbar'><h2>Füllstand & Verbrauch</h2><div class='periods'>"
  ));
  server.sendContent(F(
    "<button class='periodBtn' data-p='183'>½ Jahr</button><button class='periodBtn active' data-p='365'>1 Jahr</button>"
    "<button class='periodBtn' data-p='1825'>5 Jahre</button><button class='periodBtn' data-p='3650'>10 Jahre</button></div>"
    "<div class='chartToggles'><label><input id='showTemp' type='checkbox' checked>Temperatur</label><label><input id='showHum' type='checkbox' checked>Feuchte</label></div></div>"
    "<div id='hst' class='muted compact'>Historie wird geladen …</div><div class='chartWrap'><canvas id='chart' class='chart'></canvas><div id='chartTip' class='chartTip'></div></div>"
    "<div class='legend'><span><i style='background:#4da6ff'></i>Füllstand</span><span><i style='background:#ffb52e'></i>Verbrauch</span><span><i style='background:#42d65b'></i>Nachfüllung</span><span><i style='background:#ff8a65'></i>Temperatur</span><span><i style='background:#26c6da'></i>Luftfeuchte</span><span><i style='background:#ffd166'></i>Import</span><span><i style='background:#ff6b6b'></i>Testdaten</span></div></div>"
    "<div class='tile wide'><h2>Umgebung · AHT10</h2><div class='grid'>"
    "<div class='metric'>Temperatur<b id='ahtTemp'>-- °C</b></div><div class='metric'>Luftfeuchte<b id='ahtHum'>-- %</b></div>"
    "<div class='metric'>Taupunkt<b id='ahtDew'>-- °C</b></div><div class='metric'>Kondensationsreserve<b id='ahtReserve'>-- °C</b></div>"
    "<div class='metric'>AHT10<b id='ahtState'>--</b></div><div class='metric'>Messwertalter<b id='ahtAge'>-- s</b></div></div></div></div>"
  ));

  server.sendContent(R"JS(
<script>
(function(){
const $=i=>document.getElementById(i),f=(v,d=1)=>Number.isFinite(Number(v))?Number(v).toFixed(d):'--';
let p=365,items=[],climateItems=[],dbGeom=null,showTemp=true,showHum=true;
function updateClimateToggleState(){
  const has=climateItems.length>0;
  const t=$('showTemp'),h=$('showHum');
  if(t){t.disabled=!has;t.closest('label')?.classList.toggle('off',!has)}
  if(h){h.disabled=!has;h.closest('label')?.classList.toggle('off',!has)}
}


function dot(id,v){$(id).className='dot '+(v?'green':'red')}

function updateTankSvg(percent, liters, heightMm, distanceMm, geometry){
  const p=Math.max(0,Math.min(100,Number(percent)||0));
  const isCyl=geometry==='cylinder';
  const rg=$('tankRectGroup'),cg=$('tankCylGroup');
  if(rg)rg.style.display=isCyl?'none':'';
  if(cg)cg.style.display=isCyl?'':'none';
  $('tankShapeText').textContent=isCyl?'Liegender Zylindertank':'Batterie-/Quader-Tank';

  if(isCyl){
    const top=72,innerH=112,bottom=184,fillH=Math.max(6,(p/100)*innerH),y=bottom-fillH;
    const oil=$('oilFillCyl'),wave=$('oilWaveCyl');
)JS");
  server.sendContent(R"JS(
    if(oil){oil.setAttribute('y',y.toFixed(1));oil.setAttribute('height',fillH.toFixed(1))}
    if(wave){
      const wy=Math.max(top+5,y);
      wave.setAttribute('d','M28 '+wy+' C48 '+(wy-4)+' 68 '+(wy+4)+' 88 '+wy+' C108 '+(wy-4)+' 128 '+(wy+4)+' 148 '+wy+' C168 '+(wy-4)+' 188 '+(wy+4)+' 212 '+wy+' L212 184 L28 184 Z')
    }
  }else{
    const top=42,innerH=158,bottom=200,fillH=Math.max(6,(p/100)*innerH),y=bottom-fillH;
    const oil=$('oilFillRect'),wave=$('oilWaveRect');
    if(oil){oil.setAttribute('y',y.toFixed(1));oil.setAttribute('height',fillH.toFixed(1))}
    if(wave){
      const wy=Math.max(top+5,y);
      wave.setAttribute('d','M56 '+wy+' C70 '+(wy-4)+' 84 '+(wy+4)+' 98 '+wy+' C112 '+(wy-4)+' 126 '+(wy+4)+' 140 '+wy+' C154 '+(wy-4)+' 168 '+(wy+4)+' 184 '+wy+' L184 200 L56 200 Z')
    }
  }

)JS");
  server.sendContent(R"JS(
  const low=p<20,mid=p>=20&&p<40;
  const topStop=$('oilTop'),botStop=$('oilBottom');
  if(topStop)topStop.setAttribute('stop-color',low?'#ff7a70':(mid?'#ffd35a':'#7edb88'));
  if(botStop)botStop.setAttribute('stop-color',low?'#b42424':(mid?'#c97a00':'#23863a'));

  $('tankPercentText').textContent=f(p)+' %';
  $('tankLitersText').textContent=f(liters)+' L';
  $('tankHeightText').textContent=f(heightMm)+' mm';
  $('tankDistanceText').textContent=f(distanceMm)+' mm';
}

async function status(){
  try{
