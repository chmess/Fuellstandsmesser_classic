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
    const r=await fetch('/api/status?x='+Date.now(),{cache:'no-store'});
    if(!r.ok)throw 0;
    const d=await r.json();
    $('liters').textContent=f(d.level_liters);
    $('percent').textContent=f(d.level_percent)+' %';
    $('height').textContent=f(d.level_height_mm)+' mm';
)JS");
  server.sendContent(R"JS(
    $('distance').textContent=f(d.filtered_distance_mm)+' mm';
    $('lpm').textContent=f(d.liters_per_mm,2)+' L/mm';
    $('rssi').textContent=f(d.rssi,0)+' dBm';
    $('c1').textContent=f(d.consumption_today_l)+' L';
    $('c7').textContent=f(d.consumption_7d_l)+' L';
    $('c30').textContent=f(d.consumption_30d_l)+' L';
    $('c365').textContent=f(d.consumption_365d_l)+' L';
    $('ahtTemp').textContent=f(d.temperature_c)+' °C';
    $('ahtHum').textContent=f(d.humidity_percent)+' %';
    $('ahtDew').textContent=f(d.dew_point_c)+' °C';
    $('ahtReserve').textContent=f(d.condensation_reserve_c)+' °C';
    $('ahtState').textContent=d.aht_status||'--';
    $('ahtAge').textContent=(d.aht_age_s==null?'--':d.aht_age_s)+' s';
    dot('dw',d.wifi_connected);dot('dm',d.mqtt_connected);dot('dt',d.vl_ok);
)JS");
  server.sendContent(R"JS(
    let q=Math.max(0,Math.min(100,Number(d.level_percent)||0));
    updateTankSvg(d.level_percent, d.level_liters, d.level_height_mm, d.filtered_distance_mm, d.tank_geometry);
    $('bar').style.width=q+'%';
    $('bar').style.background=q<20?'#ff4d4d':q<40?'#ffad33':'#42d65b';
    $('updated').textContent=(d.date||'')+' '+(d.time||'');
  }catch(e){$('updated').textContent='Status-API Fehler'}
}

function draw(){
  const c=$('chart'),r=c.getBoundingClientRect(),w=Math.max(300,Math.floor(r.width)),h=Math.floor(r.height),z=devicePixelRatio||1;
  c.width=w*z;c.height=h*z;const x=c.getContext('2d');x.setTransform(z,0,0,z,0,0);x.clearRect(0,0,w,h);
  if(!items.length){x.fillStyle='#777';x.fillText('Keine Daten',20,30);return}
)JS");
  server.sendContent(R"JS(
  const pl=38,pr=showTemp&&climateItems.length?42:8,pt=10,pb=22,iw=w-pl-pr,ih=h-pt-pb,t0=items[0].time,t1=Math.max(t0+86400000,items[items.length-1].time),px=t=>pl+(t-t0)/(t1-t0)*iw,py=v=>pt+ih-Math.max(0,Math.min(100,v))/100*ih;
  let tMin=0,tMax=40;
  if(showTemp&&climateItems.length){let mn=999,mx=-999;climateItems.forEach(a=>[a.tMin,a.tAvg,a.tMax].forEach(v=>{v=Number(v);if(Number.isFinite(v)){mn=Math.min(mn,v);mx=Math.max(mx,v)}}));if(mn!==999){if(mx-mn<4){mn-=2;mx+=2}tMin=Math.floor(mn-1);tMax=Math.ceil(mx+1)}}
  const pyT=v=>pt+ih-(v-tMin)/(tMax-tMin)*ih;
  x.strokeStyle='#333';[0,25,50,75,100].forEach(v=>{let y=py(v);x.beginPath();x.moveTo(pl,y);x.lineTo(w-pr,y);x.stroke();x.fillStyle='#888';x.font='10px Arial';x.fillText(v+'%',2,y+3)});
)JS");
  server.sendContent(R"JS(
  if(showTemp&&climateItems.length)for(let i=0;i<=4;i++){const y=pt+ih-(i/4)*ih,s=(tMin+(tMax-tMin)*(i/4)).toFixed(0)+'°';x.fillStyle='#ff9d83';x.fillText(s,w-x.measureText(s).width-2,y+3)}
  const maxC=Math.max(1,...items.map(a=>Number(a.consumedLiters)||0));items.forEach(a=>{const q=px(a.time),bh=((Number(a.consumedLiters)||0)/maxC)*(ih*.32);if(bh>0){x.fillStyle='#ffb52e';x.fillRect(q-1,pt+ih-bh,2,bh)}});
  x.beginPath();items.forEach((a,i)=>{let q=px(a.time),y=py(a.percent);i?x.lineTo(q,y):x.moveTo(q,y)});x.strokeStyle='#4da6ff';x.lineWidth=2;x.stroke();
)JS");
  server.sendContent(R"JS(
  if(showHum&&climateItems.length){let begun=false;x.beginPath();climateItems.forEach(a=>{const v=Number(a.hAvg);if(!Number.isFinite(v))return;const q=px(a.time),y=py(v);begun?x.lineTo(q,y):x.moveTo(q,y);begun=true});if(begun){x.strokeStyle='#26c6da';x.lineWidth=1.8;x.stroke()}}
  if(showTemp&&climateItems.length){let begun=false;x.beginPath();climateItems.forEach(a=>{const v=Number(a.tAvg);if(!Number.isFinite(v))return;const q=px(a.time),y=pyT(v);begun?x.lineTo(q,y):x.moveTo(q,y);begun=true});if(begun){x.strokeStyle='#ff8a65';x.lineWidth=1.8;x.stroke()}}
)JS");
  server.sendContent(R"JS(
  items.forEach(a=>{const q=px(a.time),y=py(a.percent),src=Number(a.source)||0;if(src===1){x.strokeStyle='#ffd166';x.lineWidth=1.5;x.beginPath();x.arc(q,y,4,0,Math.PI*2);x.stroke()}else if(src===2){x.fillStyle='#ff6b6b';x.beginPath();x.moveTo(q,y-4);x.lineTo(q+4,y+4);x.lineTo(q-4,y+4);x.closePath();x.fill()}if(Number(a.refillLiters)>0){x.fillStyle='#42d65b';x.beginPath();x.arc(q,pt+ih-5,4,0,Math.PI*2);x.fill()}});
  dbGeom={px:items.map(a=>px(a.time))}
}

const dbC=$('chart'),dbTip=$('chartTip');
function dbShowTip(e){
  if(!dbGeom||!items.length)return;
  const r=dbC.getBoundingClientRect(),mx=(e.touches?e.touches[0].clientX:e.clientX)-r.left;
  let bi=-1,bd=99999;
  dbGeom.px.forEach((q,i)=>{let dd=Math.abs(q-mx);if(dd<bd){bd=dd;bi=i}});
  if(bi<0||bd>28){dbTip.style.display='none';return}
)JS");
  server.sendContent(R"JS(
  const a=items[bi],sn=Number(a.source)===1?'Import':(Number(a.source)===2?'Test':'Gemessen');
  dbTip.innerHTML='<b>'+new Date(a.time).toLocaleDateString('de-DE')+'</b><br>Füllstand: '+f(a.percent)+' %<br>Menge: '+f(a.liters)+' L<br>Verbrauch: '+f(a.consumedLiters)+' L<br>Quelle: '+sn+(Number(a.refillLiters)>0?'<br><span style="color:#65e572">Nachfüllung: +'+f(a.refillLiters)+' L</span>':'');
  dbTip.style.display='block';dbTip.style.left=Math.max(5,Math.min(dbC.clientWidth-185,mx+10))+'px';dbTip.style.top='8px';
}
dbC.onmousemove=dbShowTip;dbC.ontouchmove=dbShowTip;dbC.onmouseleave=()=>dbTip.style.display='none';dbC.ontouchend=()=>dbTip.style.display='none';

async function hist(){
  try{
    $('hst').textContent='Historie wird geladen …';
)JS");
  server.sendContent(R"JS(
    let r=await fetch('/api/history?days='+p+'&x='+Date.now(),{cache:'no-store'}),raw=await r.text();if(!r.ok)throw new Error('HTTP '+r.status);
    items=(JSON.parse(raw).items||[]);climateItems=[];
    try{let cr=await fetch('/api/history/climate?days='+p+'&x='+Date.now(),{cache:'no-store'});if(cr.ok)climateItems=(JSON.parse(await cr.text()).items||[])}catch(_){}
    $('hst').textContent='Geladen: '+items.length+' Punkte'+(climateItems.length?' · Klima '+climateItems.length+' Tage':' · keine Klimadaten');updateClimateToggleState();draw();
  }catch(e){items=[];climateItems=[];$('hst').textContent='Historie-Fehler: '+e.message;updateClimateToggleState();draw()}
}

document.querySelectorAll('.periodBtn').forEach(b=>b.onclick=()=>{p=Number(b.dataset.p);document.querySelectorAll('.periodBtn').forEach(q=>q.classList.toggle('active',q===b));hist()});
)JS");
  server.sendContent(R"JS(
$('showTemp').onchange=e=>{showTemp=!!e.target.checked;draw()};
$('showHum').onchange=e=>{showHum=!!e.target.checked;draw()};
updateTankSvg(0, NaN, NaN, NaN, 'rect');
updateClimateToggleState();
async function initDashboard(){await status();await hist();}
initDashboard();
setInterval(status,10000);
setInterval(hist,300000);
addEventListener('resize',draw);
})();
</script>
)JS");

  webStreamEnd();

  Serial.print(F("[WEB ROOT] end heap="));
  Serial.print(ESP.getFreeHeap());
  Serial.print(F(" maxBlock="));
  Serial.print(ESP.getMaxFreeBlockSize());
  Serial.print(F(" delta="));
  Serial.println((int32_t)ESP.getFreeHeap()-(int32_t)rootHeapBefore);
}

String checked(bool v) {
  return v ? " checked" : "";
}

void handleSettings() {
  webStreamBegin(F("Einstellungen"));
  webStreamNav(2);

  server.sendContent(F(
    "<style>"
    ".settingsGrid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px;align-items:start}"
    ".settingsBlock{background:#1d1d1d;border-radius:12px;padding:16px;box-shadow:0 2px 8px #000}"
    ".settingsBlock h2{margin:0 0 5px;color:#4dd0e1;font-size:1.15rem}"
    ".settingsBlock p{margin:0 0 12px;color:#999;font-size:.86rem}"
    ".settingsBlock label{display:block;color:#ddd;font-size:.9rem;margin-top:9px}"
    ".settingsWide{grid-column:1/-1}"
    ".settingsActions{grid-column:1/-1;text-align:center;margin-top:2px}"
    ".tankCfg{display:grid;grid-template-columns:minmax(230px,1fr) minmax(210px,.9fr);gap:14px;align-items:center}"
    ".tankPreview{background:#151a20;border:1px solid #303a44;border-radius:14px;padding:12px;text-align:center}"
    ".tankPreview svg{width:100%;max-width:260px;height:auto}"
    ".tankCalc{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;margin-top:10px}"
    ".tankCalc .metric{background:#252c33;border-radius:9px;padding:8px}"
    ".tankHint{color:#8ea0af;font-size:.82rem;margin-top:8px}"
    ".dimMuted{opacity:.38}.dimActive{opacity:1}"
    ".checkGrid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:7px;margin-top:8px}"
    ".checkGrid label{margin:0;background:#252c33;border-radius:8px;padding:8px}"
    ".inlineCheck{width:auto!important;margin-right:6px!important}"
    ".displayPages{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:7px;margin-top:8px}"
    ".displayPageBtn{background:#292f35;border:1px solid #4b5965;color:#eee;border-radius:9px;padding:8px}"
    ".displayPageBtn.active{background:#1769aa;border-color:#39a9ff}"
    ".displayPageStatus{margin-top:8px;color:#9fb0bd;font-size:.84rem}"
    "@media(max-width:760px){.settingsGrid{grid-template-columns:1fr}.tankCfg{grid-template-columns:1fr}.tankCalc{grid-template-columns:1fr 1fr}}"
    "</style>"
    "<form method='POST' action='/save'>"
    "<div class='settingsGrid'>"
  ));

  server.sendContent(F(
    "<div class='settingsBlock'><h2>WLAN</h2><p>Netzwerkzugang des Geräts.</p>"
    "<label>SSID</label><input name='ssid' value='"
  ));
  webSendSafe(String(cfg.wifiSsid));
  server.sendContent(F(
    "'><label>Passwort</label><input type='password' name='wpass' value='"
  ));
  webSendSafe(String(cfg.wifiPass));
  server.sendContent(F("'></div>"));

  server.sendContent(F(
    "<div class='settingsBlock'><h2>Sensor</h2><p>ToF-Auswahl und Kalibrierung.</p>"
    "<label>Typ</label><select name='sensor'><option value='0'"
  ));
  if(cfg.sensorType==SENSOR_AUTO) server.sendContent(F(" selected"));
  server.sendContent(F(">Auto</option><option value='1'"));
  if(cfg.sensorType==SENSOR_VL53L0X) server.sendContent(F(" selected"));
  server.sendContent(F(">VL53L0X</option><option value='2'"));
  if(cfg.sensorType==SENSOR_VL53L1X) server.sendContent(F(" selected"));
  server.sendContent(F(
    ">VL53L1X</option></select>"
    "<label>Sensor-Offset mm</label><input type='number' name='offset' value='"
  ));
  webSendSafe(String(cfg.sensorOffsetMm));
  server.sendContent(F(
    "'><label>Leer-Distanz mm</label><input type='number' step='.1' name='empty' value='"
  ));
  webSendSafe(String(cfg.emptyDistanceMm,1));
  server.sendContent(F(
    "'><label>Voll-Distanz mm</label><input type='number' step='.1' name='full' value='"
  ));
  webSendSafe(String(cfg.fullDistanceMm,1));
  server.sendContent(F("'></div>"));

  server.sendContent(F(
    "<div class='settingsBlock'><h2>AHT10 Klima</h2>"
    "<p>I²C 0x38 · Temperatur, Luftfeuchte, Taupunkt und Kondensationsreserve.</p>"
    "<label><input class='inlineCheck' type='checkbox' name='ahtEnabled'"
  ));
  if(cfg.ahtEnabled)server.sendContent(F(" checked"));
  server.sendContent(F(
    ">AHT10 aktiv</label>"
    "<label>Temperatur-Offset °C</label>"
    "<input type='number' step='0.1' min='-20' max='20' name='ahtTempOffset' value='"
  ));
  webSendSafe(String(cfg.ahtTemperatureOffsetC,1));
  server.sendContent(F(
    "'><label>Feuchte-Offset %</label>"
    "<input type='number' step='0.1' min='-50' max='50' name='ahtHumOffset' value='"
  ));
  webSendSafe(String(cfg.ahtHumidityOffsetPercent,1));
  server.sendContent(F(
    "'><label>Messintervall Sekunden</label>"
    "<input type='number' min='2' max='300' step='1' name='ahtIntervalS' value='"
  ));
  webSendSafe(String(cfg.ahtIntervalMs/1000UL));
  server.sendContent(F(
    "'><p class='tankHint'>Taupunkt und Kondensationsreserve werden aus den korrigierten Messwerten berechnet.</p>"
    "</div>"
  ));

  server.sendContent(F(
    "<div class='settingsBlock settingsWide'><h2>Tank</h2><p>Abmessungen, Geometrie und berechnete Kapazität.</p>"
    "<div class='tankCfg'><div>"
    "<label>Geometrie</label><select id='tankGeometry' name='geometry'><option value='0'"
  ));
  if(cfg.geometry==GEOMETRY_RECT) server.sendContent(F(" selected"));
  server.sendContent(F(">Quader / Batterietank</option><option value='1'"));
  if(cfg.geometry==GEOMETRY_CYLINDER) server.sendContent(F(" selected"));
  server.sendContent(F(">Zylindertank</option></select>"));

  server.sendContent(F("<div id='dimLength'><label>Länge mm</label><input id='tankLength' type='number' name='length' min='100' step='1' value='"));
  webSendSafe(String(cfg.tankLengthMm,0));
  server.sendContent(F("'></div>"));

  server.sendContent(F("<div id='dimWidth'><label>Breite mm</label><input id='tankWidth' type='number' name='width' min='100' step='1' value='"));
  webSendSafe(String(cfg.tankWidthMm,0));
  server.sendContent(F("'></div>"));

  server.sendContent(F("<div id='dimHeight'><label id='tankHeightLabel'>Tankhöhe mm</label><input id='tankHeight' type='number' name='height' min='100' step='1' value='"));
  webSendSafe(String(cfg.tankHeightMm,0));
  server.sendContent(F("'></div>"));

  server.sendContent(F("<div id='dimDiameter'><label>Durchmesser mm</label><input id='tankDiameter' type='number' name='diameter' min='100' step='1' value='"));
  webSendSafe(String(cfg.diameterMm,0));
  server.sendContent(F(
    "'></div><div id='tankFormulaHint' class='tankHint'></div>"
    "</div>"
    "<div class='tankPreview'>"
    "<svg viewBox='0 0 260 220' xmlns='http://www.w3.org/2000/svg' aria-label='Tankvorschau'>"
    "<defs>"
    "<linearGradient id='cfgBody' x1='0' x2='0' y1='0' y2='1'><stop offset='0%' stop-color='#dfe6ec'/><stop offset='100%' stop-color='#8996a2'/></linearGradient>"
    "<linearGradient id='cfgOil' x1='0' x2='0' y1='0' y2='1'><stop offset='0%' stop-color='#ffd45c'/><stop offset='100%' stop-color='#c57a00'/></linearGradient>"
    "</defs>"
    "<g id='cfgRectShape'><rect x='68' y='22' width='124' height='150' rx='28' fill='url(#cfgBody)' stroke='#52606c' stroke-width='3'/>"
    "<rect x='78' y='34' width='104' height='126' rx='18' fill='#172027'/><rect x='80' y='98' width='100' height='60' fill='url(#cfgOil)'/>"
    "<rect x='84' y='174' width='18' height='22' rx='3' fill='#6f7d89'/><rect x='158' y='174' width='18' height='22' rx='3' fill='#6f7d89'/></g>"
    "<g id='cfgCylShape' style='display:none'><rect x='30' y='56' width='200' height='106' rx='52' fill='url(#cfgBody)' stroke='#52606c' stroke-width='3'/>"
    "<rect x='40' y='66' width='180' height='86' rx='42' fill='#172027'/><path d='M40 109 H220 V152 H40 Z' fill='url(#cfgOil)'/>"
    "<rect x='64' y='164' width='22' height='18' rx='3' fill='#6f7d89'/><rect x='174' y='164' width='22' height='18' rx='3' fill='#6f7d89'/></g>"
    "<text x='130' y='208' text-anchor='middle' fill='#9fb0bd' font-size='11' id='cfgShapeName'>Quader / Batterietank</text>"
    "</svg>"
    "<div class='tankCalc'><div class='metric'>Kapazität<b id='cfgCapacity'>-- L</b></div>"
    "<div class='metric'>Tankfaktor<b id='cfgFactor'>-- L/mm</b></div></div>"
    "</div></div></div>"
  ));

  server.sendContent(F(
    "<div class='settingsBlock'><h2>Messung</h2><p>Filter- und Messgrenzen.</p>"
    "<label>Messintervall ms</label><input type='number' name='interval' value='"
  ));
  webSendUInt(cfg.measurementIntervalMs);
  server.sendContent(F("'><label>Min Abstand mm</label><input type='number' name='minD' value='"));
  webSendUInt(cfg.minDistanceMm);
  server.sendContent(F("'><label>Max Abstand mm</label><input type='number' name='maxD' value='"));
  webSendUInt(cfg.maxDistanceMm);
  server.sendContent(F("'><label>Max. Sprung mm</label><input type='number' name='jump' value='"));
  webSendUInt(cfg.maxJumpMm);
  server.sendContent(F("'></div>"));

  server.sendContent(F(
    "<div class='settingsBlock'><h2>Display</h2><p>Nokia 5110 / PCD8544.</p>"
    "<label>Kontrast: <b id='displayContrastValue'>"
  ));
  webSendUInt(cfg.displayContrast);
  server.sendContent(F("</b></label><input id='displayContrast' name='displayContrast' type='range' min='20' max='100' step='1' value='"));
  webSendUInt(cfg.displayContrast);
  server.sendContent(F("' oninput=\"document.getElementById('displayContrastValue').textContent=this.value\">"));

  server.sendContent(F("<label><input class='inlineCheck' type='checkbox' name='displayAutoRotate'"));
  if(cfg.displayAutoRotate) server.sendContent(F(" checked"));
  server.sendContent(F(">Seiten automatisch wechseln</label>"));

  server.sendContent(F("<label>Seiten im Auto-Wechsel</label><div class='checkGrid'>"));
  server.sendContent(F("<label><input class='inlineCheck' type='checkbox' name='displayAutoPage0'"));
  if(cfg.displayPageMask & 0x01) server.sendContent(F(" checked"));
  server.sendContent(F(">1 · Füllstand</label>"));

  server.sendContent(F("<label><input class='inlineCheck' type='checkbox' name='displayAutoPage1'"));
  if(cfg.displayPageMask & 0x02) server.sendContent(F(" checked"));
  server.sendContent(F(">2 · Sensor</label>"));

  server.sendContent(F("<label><input class='inlineCheck' type='checkbox' name='displayAutoPage2'"));
  if(cfg.displayPageMask & 0x04) server.sendContent(F(" checked"));
  server.sendContent(F(">3 · Netzwerk</label>"));

  server.sendContent(F("<label><input class='inlineCheck' type='checkbox' name='displayAutoPage3'"));
  if(cfg.displayPageMask & 0x08) server.sendContent(F(" checked"));
  server.sendContent(F(">4 · System</label>"));

  server.sendContent(F("<label><input class='inlineCheck' type='checkbox' name='displayAutoPage4'"));
  if(cfg.displayPageMask & 0x10) server.sendContent(F(" checked"));
  server.sendContent(F(">5 · Klima</label></div>"));

  server.sendContent(F("<label>Seitenintervall: <b id='displayPageSecondsValue'>"));
  webSendUInt(cfg.displayPageSeconds);
  server.sendContent(F(" s</b></label><input id='displayPageSeconds' name='displayPageSeconds' type='range' min='2' max='60' step='1' value='"));
  webSendUInt(cfg.displayPageSeconds);
  server.sendContent(F("' oninput=\"document.getElementById('displayPageSecondsValue').textContent=this.value+' s'\">"));

  server.sendContent(F("<label>Schriftstärke</label><select name='displayFontWeight'><option value='0'"));
  if(cfg.displayFontWeight==0) server.sendContent(F(" selected"));
  server.sendContent(F(">Normal</option><option value='1'"));
  if(cfg.displayFontWeight==1) server.sendContent(F(" selected"));
  server.sendContent(F(">Fett</option><option value='2'"));
  if(cfg.displayFontWeight==2) server.sendContent(F(" selected"));
  server.sendContent(F(">Extra-Fett</option></select>"));

  server.sendContent(F("<label><input class='inlineCheck' type='checkbox' name='displayInvert'"));
  if(cfg.displayInvert) server.sendContent(F(" checked"));
  server.sendContent(F(">Anzeige invertieren</label>"));

  server.sendContent(F(
    "<label>Displayseite manuell wählen</label>"
    "<div class='displayPages'>"
    "<button type='button' class='displayPageBtn' data-page='0'>1 · Füllstand</button>"
    "<button type='button' class='displayPageBtn' data-page='1'>2 · Sensor</button>"
    "<button type='button' class='displayPageBtn' data-page='2'>3 · Netzwerk</button>"
    "<button type='button' class='displayPageBtn' data-page='3'>4 · System</button>"
    "</div><div id='displayPageStatus' class='displayPageStatus'>Aktuelle Seite: --</div>"
    "</div>"
  ));

  server.sendContent(F(
    "<div class='settingsBlock settingsWide'><h2>MQTT</h2>"
    "<p>Kompatibilität: <b>average = Liter</b>, <b>fuellhoehe = gefilterter Sensorabstand mm</b>.</p>"
    "<label><input class='inlineCheck' type='checkbox' name='mqtt'"
  ));
  if(cfg.mqttEnabled) server.sendContent(F(" checked"));
  server.sendContent(F(">MQTT aktiv</label><div class='grid'><div><label>Broker</label><input name='mhost' value='"));
  webSendSafe(String(cfg.mqttHost));
  server.sendContent(F("'><label>Port</label><input type='number' name='mport' value='"));
  webSendUInt(cfg.mqttPort);
  server.sendContent(F("'><label>Benutzer</label><input name='muser' value='"));
  webSendSafe(String(cfg.mqttUser));
  server.sendContent(F("'><label>Passwort</label><input type='password' name='mpass' value='"));
  webSendSafe(String(cfg.mqttPass));
  server.sendContent(F("'></div><div><label>Basis-Topic</label><input name='mbase' value='"));
  webSendSafe(String(cfg.mqttBase));
  server.sendContent(F("'><label>Liter-Topic</label><input name='mavg' value='"));
  webSendSafe(String(cfg.mqttAverageTopic));
  server.sendContent(F("'><label>Fuellhoehe-Topic</label><input name='mheight' value='"));
  webSendSafe(String(cfg.mqttFuellhoeheTopic));
  server.sendContent(F(
    "'></div></div>"
    "<p class='tankHint'>Home-Assistant-Discovery ist deaktiviert. Die normalen MQTT-Topics bleiben aktiv.</p>"
    "</div>"
  ));

  server.sendContent(F(
    "<div class='settingsActions'>"
    "<button type='submit'>Speichern & Neustarten</button>"
    "<a class='btn danger' href='/factory-reset' onclick=\"return confirm('Werkseinstellungen wirklich laden?')\">Werkseinstellungen</a>"
    "</div>"
    "</div></form>"
  ));

  server.sendContent(R"JS(
<script>
(function(){
const $=i=>document.getElementById(i);
const n=i=>Math.max(0,Number($(i)?.value)||0);

function updateTankConfigPreview(){
  const g=$('tankGeometry');
  if(!g)return;
  const cyl=g.value==='1';
  const L=n('tankLength'),W=n('tankWidth'),H=n('tankHeight'),D=n('tankDiameter');
  let liters=0;

  $('cfgRectShape').style.display=cyl?'none':'';
  $('cfgCylShape').style.display=cyl?'':'none';
  $('cfgShapeName').textContent=cyl?'Zylindertank':'Quader / Batterietank';
  $('dimLength').className=cyl?'dimMuted':'dimActive';
  $('dimWidth').className=cyl?'dimMuted':'dimActive';
  $('dimDiameter').className=cyl?'dimActive':'dimMuted';
  $('dimHeight').className='dimActive';

  if(cyl){
    const r=D/2000.0;
    liters=Math.PI*r*r*(H/1000.0)*1000.0;
    $('tankHeightLabel').textContent='Zylinderlänge / Tankhöhe mm';
    $('tankFormulaHint').textContent='Zylinder: π × (Durchmesser/2)² × Tankhöhe.';
  }else{
    liters=(L/1000.0)*(W/1000.0)*(H/1000.0)*1000.0;
    $('tankHeightLabel').textContent='Tankhöhe mm';
)JS");
  server.sendContent(R"JS(
    $('tankFormulaHint').textContent='Quader: Länge × Breite × Höhe.';
  }

  $('cfgCapacity').textContent=Math.round(liters).toLocaleString('de-DE')+' L';
  $('cfgFactor').textContent=(H>0?liters/H:0).toFixed(2)+' L/mm';
}

['tankGeometry','tankLength','tankWidth','tankHeight','tankDiameter'].forEach(id=>{
  const e=$(id);
  if(!e)return;
  e.addEventListener('input',updateTankConfigPreview);
  e.addEventListener('change',updateTankConfigPreview);
});

const displayPageNames=['Füllstand','Sensor','Netzwerk','System'];

function setDisplayPageUi(page){
  page=Number(page)||0;
  document.querySelectorAll('.displayPageBtn').forEach(b=>{
    b.classList.toggle('active',Number(b.dataset.page)===page);
  });
  const st=$('displayPageStatus');
  if(st)st.textContent='Aktuelle Seite: '+(page+1)+' · '+(displayPageNames[page]||'--');
}

async function selectDisplayPage(page){
  const st=$('displayPageStatus');
  if(st)st.textContent='Display wird umgeschaltet …';
  try{
    const r=await fetch('/api/display/page',{
      method:'POST',
)JS");
  server.sendContent(R"JS(
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:'page='+encodeURIComponent(page)
    });
    const j=await r.json();
    if(!r.ok||!j.ok)throw new Error(j.error||('HTTP '+r.status));
    setDisplayPageUi(j.page);
  }catch(e){
    if(st)st.textContent='Seitenwechsel fehlgeschlagen';
  }
}

document.querySelectorAll('.displayPageBtn').forEach(b=>{
  b.addEventListener('click',()=>selectDisplayPage(Number(b.dataset.page)));
});

async function loadDisplayPage(){
  try{
    const r=await fetch('/api/status?display=1&x='+Date.now(),{cache:'no-store'});
    if(!r.ok)return;
    const j=await r.json();
    setDisplayPageUi(j.display_page||0);
  }catch(e){}
}

updateTankConfigPreview();
loadDisplayPage();
})();
</script>
)JS");

  webStreamEnd();
}

void copyArg(const char* name, char* dst, size_t len) {
  if (!server.hasArg(name)) return;
  String v = server.arg(name);
  v.trim();
  strlcpy(dst, v.c_str(), len);
}

void handleSave() {
  copyArg("ssid", cfg.wifiSsid, sizeof(cfg.wifiSsid));
  copyArg("wpass", cfg.wifiPass, sizeof(cfg.wifiPass));

  long sensorSel = server.arg("sensor").toInt();
  if (sensorSel < 0) sensorSel = 0;
  if (sensorSel > 2) sensorSel = 2;
  cfg.sensorType = (uint8_t)sensorSel;
  cfg.sensorOffsetMm = server.arg("offset").toInt();

  cfg.ahtEnabled = server.hasArg("ahtEnabled");
  cfg.ahtTemperatureOffsetC = server.arg("ahtTempOffset").toFloat();
  cfg.ahtHumidityOffsetPercent = server.arg("ahtHumOffset").toFloat();
  long ahtIntervalS = server.arg("ahtIntervalS").toInt();
  if(ahtIntervalS < 2)ahtIntervalS=2;
  if(ahtIntervalS > 300)ahtIntervalS=300;
  cfg.ahtIntervalMs=(uint32_t)ahtIntervalS*1000UL;

  cfg.emptyDistanceMm = server.arg("empty").toFloat();
  cfg.fullDistanceMm = server.arg("full").toFloat();

  long geometrySel = server.arg("geometry").toInt();
  if (geometrySel < 0) geometrySel = 0;
  if (geometrySel > 1) geometrySel = 1;
  cfg.geometry = (uint8_t)geometrySel;
  cfg.tankLengthMm = server.arg("length").toFloat();
  cfg.tankWidthMm = server.arg("width").toFloat();
  cfg.tankHeightMm = server.arg("height").toFloat();
  cfg.diameterMm = server.arg("diameter").toFloat();

  cfg.mqttEnabled = server.hasArg("mqtt");
  copyArg("mhost", cfg.mqttHost, sizeof(cfg.mqttHost));

  long mqttPort = server.arg("mport").toInt();
  if (mqttPort < 1) mqttPort = 1;
  if (mqttPort > 65535) mqttPort = 65535;
  cfg.mqttPort = (uint16_t)mqttPort;

  copyArg("muser", cfg.mqttUser, sizeof(cfg.mqttUser));
  copyArg("mpass", cfg.mqttPass, sizeof(cfg.mqttPass));
  copyArg("mbase", cfg.mqttBase, sizeof(cfg.mqttBase));
  copyArg("mavg", cfg.mqttAverageTopic, sizeof(cfg.mqttAverageTopic));
  copyArg("mheight", cfg.mqttFuellhoeheTopic, sizeof(cfg.mqttFuellhoeheTopic));


  long intervalMs = server.arg("interval").toInt();
  if (intervalMs < 500) intervalMs = 500;
  cfg.measurementIntervalMs = (uint32_t)intervalMs;

  long minD = server.arg("minD").toInt();
  if (minD < 1) minD = 1;
  if (minD > 65534) minD = 65534;
  cfg.minDistanceMm = (uint16_t)minD;

  long maxD = server.arg("maxD").toInt();
  if (maxD < (long)cfg.minDistanceMm + 1L) {
    maxD = (long)cfg.minDistanceMm + 1L;
  }
  if (maxD > 65535) maxD = 65535;
  cfg.maxDistanceMm = (uint16_t)maxD;

  long jump = server.arg("jump").toInt();
  if (jump < 1) jump = 1;
  if (jump > 65535) jump = 65535;
  cfg.maxJumpMm = (uint16_t)jump;

  long displayContrast = server.arg("displayContrast").toInt();
  if (displayContrast < 20) displayContrast = 20;
  if (displayContrast > 100) displayContrast = 100;
  cfg.displayContrast = (uint8_t)displayContrast;

  cfg.displayAutoRotate = server.hasArg("displayAutoRotate");
  cfg.displayPageMask = 0;
  if(server.hasArg("displayAutoPage0")) cfg.displayPageMask |= 0x01;
  if(server.hasArg("displayAutoPage1")) cfg.displayPageMask |= 0x02;
  if(server.hasArg("displayAutoPage2")) cfg.displayPageMask |= 0x04;
  if(server.hasArg("displayAutoPage3")) cfg.displayPageMask |= 0x08;
  if(server.hasArg("displayAutoPage4")) cfg.displayPageMask |= 0x10;
  if(cfg.displayPageMask == 0) cfg.displayPageMask = 0x01;
  long displayPageSeconds = server.arg("displayPageSeconds").toInt();
  if (displayPageSeconds < 2) displayPageSeconds = 2;
  if (displayPageSeconds > 60) displayPageSeconds = 60;
  cfg.displayPageSeconds = (uint8_t)displayPageSeconds;
  long displayFontWeight = server.arg("displayFontWeight").toInt();
  if(displayFontWeight < 0) displayFontWeight = 0;
  if(displayFontWeight > 2) displayFontWeight = 2;
  cfg.displayFontWeight = (uint8_t)displayFontWeight;

  cfg.displayInvert = server.hasArg("displayInvert");

  validateConfig(true);

  saveConfig();

  server.send(
    200,
    "text/html; charset=utf-8",
    F("<html><body><h1>Gespeichert</h1><p>Neustart...</p></body></html>")
  );

  delay(500);
  ESP.restart();
}

void handleNotFound() {
  if (apMode) {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

void jsonChunkBegin(){
  webPrepareConnectionClose();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200,"application/json; charset=utf-8","");
}

void jsonChunkEnd(){
  server.sendContent("");
  webFinishConnection();
}

void jsonSendKey(const __FlashStringHelper* key){
  server.sendContent(F("\""));
  server.sendContent(key);
  server.sendContent(F("\":"));
}

void jsonSendStringValue(const String& value){
  server.sendContent(F("\""));
  webSendSafe(value);
  server.sendContent(F("\""));
}

void jsonSendStringValue(const char* value){
  server.sendContent(F("\""));
  webSendSafe(value);
  server.sendContent(F("\""));
}

void jsonSendBoolValue(bool value){
  server.sendContent(value?F("true"):F("false"));
}

void jsonSendNumberValue(const String& value){
  webSendSafe(value);
}

void jsonSendUIntValue(uint32_t value){
  char b[16];
  ultoa(value,b,10);
  webSendSafe(b);
}

void jsonSendIntValue(int32_t value){
  char b[16];
  ltoa(value,b,10);
  webSendSafe(b);
}

void jsonSendFloatValue(float value,uint8_t decimals){
  char b[24];
  dtostrf(value,0,decimals,b);
  char* p=b;
  while(*p==' ')p++;
  webSendSafe(p);
}

void handleApiStatus() {
  const uint32_t heapBefore=ESP.getFreeHeap();

  time_t now=time(nullptr);
  struct tm tmNow={};
  char dateBuf[16]="--",timeBuf[16]="--";
  if(now>1700000000){
    localtime_r(&now,&tmNow);
    snprintf(dateBuf,sizeof(dateBuf),"%02u.%02u.%04u",
             (unsigned)tmNow.tm_mday,
             (unsigned)(tmNow.tm_mon+1),
             (unsigned)(tmNow.tm_year+1900));
    snprintf(timeBuf,sizeof(timeBuf),"%02u:%02u:%02u",
             (unsigned)tmNow.tm_hour,
             (unsigned)tmNow.tm_min,
             (unsigned)tmNow.tm_sec);
  }

  const float cap=tankCapacityLiters();
  const float lpm=(cfg.tankHeightMm>0)?cap/cfg.tankHeightMm:0.0f;
  const float c1=historyConsumptionDays(1);
  const float c7=historyConsumptionDays(7);
  const float c30=historyConsumptionDays(30);
  const float c365=historyConsumptionDays(365);

  jsonChunkBegin();
  server.sendContent(F("{"));

  jsonSendKey(F("version"));jsonSendStringValue(FW_VERSION);server.sendContent(F(","));
  jsonSendKey(F("sensor"));jsonSendStringValue(sensorName(activeSensorType));server.sendContent(F(","));
  jsonSendKey(F("tank_geometry"));jsonSendStringValue(cfg.geometry==GEOMETRY_CYLINDER?"cylinder":"rect");server.sendContent(F(","));

  jsonSendKey(F("display_page"));jsonSendUIntValue(displayPage);server.sendContent(F(","));
  jsonSendKey(F("display_auto"));jsonSendBoolValue(cfg.displayAutoRotate);server.sendContent(F(","));
  jsonSendKey(F("display_page_seconds"));jsonSendUIntValue(cfg.displayPageSeconds);server.sendContent(F(","));
  jsonSendKey(F("display_invert"));jsonSendBoolValue(cfg.displayInvert);server.sendContent(F(","));
  jsonSendKey(F("display_page_mask"));jsonSendUIntValue(cfg.displayPageMask);server.sendContent(F(","));
  jsonSendKey(F("display_font_weight"));jsonSendUIntValue(cfg.displayFontWeight);server.sendContent(F(","));

  jsonSendKey(F("vl_ok"));jsonSendBoolValue(sensorOk);server.sendContent(F(","));
  jsonSendKey(F("sensor_ok"));jsonSendBoolValue(sensorOk);server.sendContent(F(","));
  jsonSendKey(F("aht_enabled"));jsonSendBoolValue(cfg.ahtEnabled);server.sendContent(F(","));
  jsonSendKey(F("aht_ok"));jsonSendBoolValue(ahtOk);server.sendContent(F(","));
  jsonSendKey(F("aht_interval_ms"));jsonSendUIntValue(cfg.ahtIntervalMs);server.sendContent(F(","));
  jsonSendKey(F("aht_temperature_offset_c"));jsonSendFloatValue(cfg.ahtTemperatureOffsetC,1);server.sendContent(F(","));
  jsonSendKey(F("aht_humidity_offset_percent"));jsonSendFloatValue(cfg.ahtHumidityOffsetPercent,1);server.sendContent(F(","));

  jsonSendKey(F("temperature_c"));
  if(ahtOk&&isfinite(ahtTemperatureC))jsonSendFloatValue(ahtTemperatureC,1);else server.sendContent(F("null"));
  server.sendContent(F(","));

  jsonSendKey(F("humidity_percent"));
  if(ahtOk&&isfinite(ahtHumidityPercent))jsonSendFloatValue(ahtHumidityPercent,1);else server.sendContent(F("null"));
  server.sendContent(F(","));

  jsonSendKey(F("dew_point_c"));
  if(ahtOk&&isfinite(ahtDewPointC))jsonSendFloatValue(ahtDewPointC,1);else server.sendContent(F("null"));
  server.sendContent(F(","));

  jsonSendKey(F("condensation_reserve_c"));
  if(ahtOk&&isfinite(ahtCondensationReserveC))jsonSendFloatValue(ahtCondensationReserveC,1);else server.sendContent(F("null"));
  server.sendContent(F(","));

  jsonSendKey(F("aht_status"));jsonSendStringValue(ahtStatusText());server.sendContent(F(","));
  jsonSendKey(F("aht_age_s"));
  if(lastAhtValidMs>0)jsonSendUIntValue((millis()-lastAhtValidMs)/1000UL);else server.sendContent(F("null"));
  server.sendContent(F(","));


  jsonSendKey(F("raw_distance_mm"));
  if(isfinite(rawDistanceMm))jsonSendFloatValue(rawDistanceMm,1);else server.sendContent(F("null"));
  server.sendContent(F(","));

  jsonSendKey(F("filtered_distance_mm"));
  if(isfinite(filteredDistanceMm))jsonSendFloatValue(filteredDistanceMm,1);else server.sendContent(F("null"));
  server.sendContent(F(","));

  jsonSendKey(F("level_height_mm"));
  if(isfinite(tankHeightNowMm))jsonSendFloatValue(tankHeightNowMm,1);else server.sendContent(F("null"));
  server.sendContent(F(","));

  jsonSendKey(F("level_percent"));
  if(isfinite(tankPercent))jsonSendFloatValue(tankPercent,1);else server.sendContent(F("null"));
  server.sendContent(F(","));

  jsonSendKey(F("percent"));
  if(isfinite(tankPercent))jsonSendFloatValue(tankPercent,1);else server.sendContent(F("null"));
  server.sendContent(F(","));

  jsonSendKey(F("level_liters"));
  if(isfinite(tankLiters))jsonSendFloatValue(tankLiters,1);else server.sendContent(F("null"));
  server.sendContent(F(","));

  jsonSendKey(F("liters"));
  if(isfinite(tankLiters))jsonSendFloatValue(tankLiters,1);else server.sendContent(F("null"));
  server.sendContent(F(","));

  jsonSendKey(F("liters_per_mm"));jsonSendFloatValue(lpm,2);server.sendContent(F(","));
  jsonSendKey(F("consumption_today_l"));jsonSendFloatValue(c1,1);server.sendContent(F(","));
  jsonSendKey(F("consumption_7d_l"));jsonSendFloatValue(c7,1);server.sendContent(F(","));
  jsonSendKey(F("consumption_30d_l"));jsonSendFloatValue(c30,1);server.sendContent(F(","));
  jsonSendKey(F("consumption_365d_l"));jsonSendFloatValue(c365,1);server.sendContent(F(","));
  jsonSendKey(F("consumption_avg_30d_l_day"));jsonSendFloatValue(c30/30.0f,1);server.sendContent(F(","));

  jsonSendKey(F("wifi_connected"));jsonSendBoolValue(WiFi.status()==WL_CONNECTED);server.sendContent(F(","));
  jsonSendKey(F("mqtt_connected"));jsonSendBoolValue(mqttClient.connected());server.sendContent(F(","));
  jsonSendKey(F("spiffs_ready"));jsonSendBoolValue(fsMounted);server.sendContent(F(","));
  jsonSendKey(F("history_ready"));jsonSendBoolValue(historyReady);server.sendContent(F(","));
  jsonSendKey(F("ota_ready"));jsonSendBoolValue(true);server.sendContent(F(","));

  jsonSendKey(F("rssi"));
  jsonSendIntValue(WiFi.status()==WL_CONNECTED?WiFi.RSSI():0);
  server.sendContent(F(","));

  jsonSendKey(F("date"));jsonSendStringValue(dateBuf);server.sendContent(F(","));
  jsonSendKey(F("time"));jsonSendStringValue(timeBuf);server.sendContent(F(","));

  jsonSendKey(F("mqtt_publish_ok"));jsonSendUIntValue(mqttPublishCount);server.sendContent(F(","));
  jsonSendKey(F("mqtt_publish_errors"));jsonSendUIntValue(mqttPublishErrors);

  server.sendContent(F("}"));
  jsonChunkEnd();

  const uint32_t heapAfter=ESP.getFreeHeap();
  Serial.print(F("[API STATUS] heap before="));
  Serial.print(heapBefore);
  Serial.print(F(" after="));
  Serial.print(heapAfter);
  Serial.print(F(" delta="));
  Serial.println((int32_t)heapAfter-(int32_t)heapBefore);
}


void handleHealthApi() {
  const uint32_t heapBefore=ESP.getFreeHeap();

  jsonChunkBegin();
  server.sendContent(F("{"));

  jsonSendKey(F("version"));jsonSendStringValue(String(FW_VERSION));server.sendContent(F(","));
  jsonSendKey(F("uptime_s"));jsonSendNumberValue(String(millis()/1000UL));server.sendContent(F(","));
  jsonSendKey(F("heap_free"));jsonSendNumberValue(String(ESP.getFreeHeap()));server.sendContent(F(","));
  jsonSendKey(F("heap_min"));jsonSendNumberValue(String(minFreeHeapSeen));server.sendContent(F(","));
  jsonSendKey(F("max_block"));jsonSendNumberValue(String(ESP.getMaxFreeBlockSize()));server.sendContent(F(","));
  jsonSendKey(F("max_block_min"));jsonSendNumberValue(String(lowestMaxBlockSeen?lowestMaxBlockSeen:ESP.getMaxFreeBlockSize()));server.sendContent(F(","));
  jsonSendKey(F("heap_frag_pct"));jsonSendNumberValue(String(ESP.getHeapFragmentation()));server.sendContent(F(","));
  jsonSendKey(F("heap_frag_max_pct"));jsonSendNumberValue(String(highestHeapFragSeen));server.sendContent(F(","));
  jsonSendKey(F("web_requests"));jsonSendNumberValue(String(webRequestCount));server.sendContent(F(","));
  jsonSendKey(F("low_heap_events"));jsonSendNumberValue(String(webLowHeapEvents));server.sendContent(F(","));
  jsonSendKey(F("history_api_requests"));jsonSendNumberValue(String(historyApiRequests));server.sendContent(F(","));
  jsonSendKey(F("history_api_errors"));jsonSendNumberValue(String(historyApiErrors));server.sendContent(F(","));
  jsonSendKey(F("history_api_last_ms"));jsonSendNumberValue(String(historyApiLastMs));server.sendContent(F(","));
  jsonSendKey(F("history_api_last_items"));jsonSendNumberValue(String(historyApiLastItems));server.sendContent(F(","));
  jsonSendKey(F("history_write_errors"));jsonSendNumberValue(String(historyWriteErrors));server.sendContent(F(","));
  jsonSendKey(F("history_repair_duplicates"));jsonSendNumberValue(String(historyRepairDuplicates));server.sendContent(F(","));
  jsonSendKey(F("history_repair_invalid"));jsonSendNumberValue(String(historyRepairInvalid));server.sendContent(F(","));
  jsonSendKey(F("history_repair_removed"));jsonSendNumberValue(String(historyRepairRemoved));server.sendContent(F(","));
  jsonSendKey(F("history_repair_performed"));jsonSendBoolValue(historyRepairPerformed);server.sendContent(F(","));
  jsonSendKey(F("history_compact_duplicates"));jsonSendNumberValue(String(historyCompactDuplicates));server.sendContent(F(","));
  jsonSendKey(F("history_compact_invalid"));jsonSendNumberValue(String(historyCompactInvalid));server.sendContent(F(","));
  jsonSendKey(F("history_compact_performed"));jsonSendBoolValue(historyCompactPerformed);server.sendContent(F(","));
  jsonSendKey(F("history_current_day"));jsonSendNumberValue(historyCurrentValid?String(historyCurrent.dayKey):String(0));server.sendContent(F(","));
  jsonSendKey(F("history_current_source"));jsonSendNumberValue(historyCurrentValid?String(historyCurrent.source):String(255));server.sendContent(F(","));
  jsonSendKey(F("mqtt_publish_errors"));jsonSendNumberValue(String(mqttPublishErrors));server.sendContent(F(","));
  jsonSendKey(F("mqtt_connect_errors"));jsonSendNumberValue(String(mqttConnectErrors));server.sendContent(F(","));
  jsonSendKey(F("mqtt_retry_ms"));jsonSendNumberValue(String(mqttRetryIntervalMs));server.sendContent(F(","));
  jsonSendKey(F("mqtt_dns_ok"));jsonSendBoolValue(mqttLastDnsOk);server.sendContent(F(","));
  jsonSendKey(F("mqtt_tcp_ok"));jsonSendBoolValue(mqttLastTcpOk);server.sendContent(F(","));
  jsonSendKey(F("mqtt_last_connect_ms"));jsonSendNumberValue(String(mqttLastConnectDurationMs));server.sendContent(F(","));

  jsonSendKey(F("measurement_errors"));jsonSendNumberValue(String(measurementErrors));server.sendContent(F(","));
  jsonSendKey(F("aht_ok"));jsonSendBoolValue(ahtOk);server.sendContent(F(","));
  jsonSendKey(F("aht_reads"));jsonSendNumberValue(String(ahtReadCount));server.sendContent(F(","));
  jsonSendKey(F("aht_errors"));jsonSendNumberValue(String(ahtErrorCount));server.sendContent(F(","));
  jsonSendKey(F("aht_recoveries"));jsonSendNumberValue(String(ahtRecoveryCount));server.sendContent(F(","));
  jsonSendKey(F("aht_initialized"));jsonSendBoolValue(ahtInitialized);server.sendContent(F(","));
  jsonSendKey(F("aht_probes"));jsonSendNumberValue(String(ahtProbeCount));server.sendContent(F(","));
  jsonSendKey(F("aht_consecutive_errors"));jsonSendNumberValue(String(ahtConsecutiveErrors));server.sendContent(F(","));
  jsonSendKey(F("aht_status"));jsonSendStringValue(String(ahtStatusText()));server.sendContent(F(","));
  jsonSendKey(F("aht_age_s"));jsonSendNumberValue(lastAhtValidMs?String((millis()-lastAhtValidMs)/1000UL):String(0));server.sendContent(F(","));
  jsonSendKey(F("startup_filter_stable"));jsonSendBoolValue(startupMeasurementStable);server.sendContent(F(","));
  jsonSendKey(F("startup_filter_confirm"));jsonSendNumberValue(String(startupCandidateCount));server.sendContent(F(","));
  jsonSendKey(F("wifi_reconnects"));jsonSendNumberValue(String(wifiReconnectCount));server.sendContent(F(","));
  jsonSendKey(F("cli_commands"));jsonSendNumberValue(String(cliCommandCount));server.sendContent(F(","));
  jsonSendKey(F("cli_errors"));jsonSendNumberValue(String(cliErrorCount));server.sendContent(F(","));
  jsonSendKey(F("cli_wifi_dirty"));jsonSendBoolValue(cliWifiDirty);server.sendContent(F(","));
  jsonSendKey(F("web_ota_attempts"));jsonSendNumberValue(String(webOtaAttempts));server.sendContent(F(","));
  jsonSendKey(F("web_ota_success"));jsonSendNumberValue(String(webOtaSuccessCount));server.sendContent(F(","));
  jsonSendKey(F("web_ota_errors"));jsonSendNumberValue(String(webOtaErrorCount));server.sendContent(F(","));
  jsonSendKey(F("reset"));jsonSendStringValue(String(resetReasonShort()));

  server.sendContent(F("}"));
  jsonChunkEnd();

  const uint32_t heapAfter=ESP.getFreeHeap();
  Serial.print(F("[API HEALTH] heap before="));
  Serial.print(heapBefore);
  Serial.print(F(" after="));
  Serial.print(heapAfter);
  Serial.print(F(" delta="));
  Serial.println((int32_t)heapAfter-(int32_t)heapBefore);
}


void handleWebOtaPage() {
  webStreamBegin(F("Web OTA"));
  webStreamNav(3);
  server.sendContent(F(
    "<div class='card'><h1>Firmware Update</h1>"
    "<p>ESP8266 Firmware als <b>.bin</b> hochladen.</p>"
    "<p class='muted'>Während des Uploads Messung, MQTT und Historie nicht bedienen. "
    "Nach erfolgreichem Update startet das Gerät automatisch neu.</p>"
    "<div class='grid'>"
    "<div class='metric-card'><h3>Firmware</h3><div>"
  ));
  server.sendContent(String(FW_VERSION));
  server.sendContent(F(
    "</div></div><div class='metric-card'><h3>Freier Sketch-/OTA-Speicher</h3><div>"
  ));
  server.sendContent(String(ESP.getFreeSketchSpace()/1024UL));
  server.sendContent(F(
    " KB</div></div><div class='metric-card'><h3>Heap frei</h3><div>"
  ));
  server.sendContent(String(ESP.getFreeHeap()/1024.0f,1));
  server.sendContent(F(
    " KB</div></div></div>"
    "<form method='POST' action='/update' enctype='multipart/form-data' "
    "onsubmit=\"document.getElementById('upbtn').disabled=true;document.getElementById('msg').textContent='Upload läuft …';\">"
    "<input type='file' name='firmware' accept='.bin,application/octet-stream' required>"
    "<button id='upbtn' type='submit'>Firmware hochladen</button></form>"
    "<p id='msg' class='muted'></p>"
    "<p><a class='btn' href='/systemstatus'>Zurück</a></p></div>"
  ));
  webStreamEnd();
}

void handleWebOtaUpload() {
  HTTPUpload& upload = server.upload();

  if(upload.status == UPLOAD_FILE_START) {
    webOtaActive = true;
    webOtaAttempts++;
    webOtaSuccess = false;
    webOtaBytes = 0;
    webOtaError = "";

    Serial.print(F("[WEB OTA] Start Datei="));
    Serial.println(upload.filename);
    Serial.print(F("[WEB OTA] Heap="));
    Serial.println(ESP.getFreeHeap());

    WiFiUDP::stopAll();
    mqttClient.disconnect();

    const uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if(!Update.begin(maxSketchSpace)) {
      webOtaError = Update.getErrorString();
      webOtaErrorCount++;
      Serial.print(F("[WEB OTA] Update.begin FEHLER: "));
      Serial.println(webOtaError);
    }
  }
  else if(upload.status == UPLOAD_FILE_WRITE) {
    if(webOtaError.length()) return;

    const size_t written = Update.write(upload.buf, upload.currentSize);
    webOtaBytes += written;

    if(written != upload.currentSize) {
      webOtaError = Update.getErrorString();
      webOtaErrorCount++;
      Serial.print(F("[WEB OTA] Schreibfehler: "));
      Serial.println(webOtaError);
      return;
    }

    if((webOtaBytes & 0xFFFFUL) < upload.currentSize) {
      Serial.print(F("[WEB OTA] "));
      Serial.print(webOtaBytes/1024UL);
      Serial.println(F(" KB"));
    }

    yield();
  }
  else if(upload.status == UPLOAD_FILE_END) {
    if(!webOtaError.length()) {
      if(Update.end(true)) {
        webOtaSuccess = true;
        webOtaSuccessCount++;
        Serial.print(F("[WEB OTA] OK Bytes="));
        Serial.println(webOtaBytes);
      } else {
        webOtaError = Update.getErrorString();
        webOtaErrorCount++;
        Serial.print(F("[WEB OTA] Update.end FEHLER: "));
        Serial.println(webOtaError);
      }
    }
  }
  else if(upload.status == UPLOAD_FILE_ABORTED) {
    webOtaError = F("Upload abgebrochen");
    webOtaErrorCount++;
    Serial.println(F("[WEB OTA] abgebrochen"));
  }
}

void handleWebOtaDone() {
  webOtaActive = false;

  if(webOtaSuccess) {
    server.send(200, "text/html; charset=utf-8",
      "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<meta http-equiv='refresh' content='12;url=/'></head><body style='font-family:Arial;background:#111;color:#eee;padding:30px'>"
      "<h1 style='color:#65e572'>Update erfolgreich</h1>"
      "<p>Das Gerät startet jetzt neu.</p><p>Die Startseite wird automatisch neu geladen.</p></body></html>");
    delay(250);
    ESP.restart();
    return;
  }

  String msg = F("Web OTA fehlgeschlagen: ");
  msg += webOtaError.length() ? webOtaError : F("unbekannter Fehler");
  server.send(500, "text/plain; charset=utf-8", msg);
}

void webMetricCard(const __FlashStringHelper* title,const String& value){
  server.sendContent(F("<div class='metric-card'><h3>"));
  server.sendContent(title);
  server.sendContent(F("</h3><div>"));
  webSendSafe(value);
  server.sendContent(F("</div></div>"));
}

void webMetricCardUInt(const __FlashStringHelper* title,uint32_t value,const __FlashStringHelper* suffix){
  server.sendContent(F("<div class='metric'><span>"));
  server.sendContent(title);
  server.sendContent(F("</span><b>"));
  webSendUInt(value);
  if(suffix)server.sendContent(suffix);
  server.sendContent(F("</b></div>"));
}

void webMetricCardFloat(const __FlashStringHelper* title,float value,uint8_t decimals,const __FlashStringHelper* suffix){
  server.sendContent(F("<div class='metric'><span>"));
  server.sendContent(title);
  server.sendContent(F("</span><b>"));
  webSendFloat(value,decimals);
  if(suffix)server.sendContent(suffix);
  server.sendContent(F("</b></div>"));
}

void webTableRow(const __FlashStringHelper* label,const String& value){
  server.sendContent(F("<tr><td>"));
  server.sendContent(label);
  server.sendContent(F("</td><td>"));
  webSendSafe(value);
  server.sendContent(F("</td></tr>"));
}

void handleSystemStatusPage() {
  const uint32_t heapBefore=ESP.getFreeHeap();
  Serial.print(F("[WEB] /systemstatus heap before="));
  Serial.println(heapBefore);

  webStreamBegin(F("System"));
  webStreamNav(3);

  server.sendContent(F(
    "<div class='card'><div class='topbar'><h1>System</h1><div class='links'>"
    "<a class='btn' href='/storage'>Speicher</a>"
    "<a class='btn' href='/update'>Web OTA</a>"
    "<a class='btn' href='/api/status'>Status JSON</a>"
    "<a class='btn' href='/api/health'>Health JSON</a>"
    "</div></div><div class='grid'>"
  ));

  webMetricCard(F("Firmware"),String(FW_VERSION));
  webMetricCard(F("Gerät"),String(F("ESP8266 D1 mini")));
  webMetricCard(F("Uptime"),String(millis()/1000UL)+F(" s"));
  webMetricCardFloat(F("Heap frei"),ESP.getFreeHeap()/1024.0f,1,F(" KB"));
  webMetricCardFloat(F("Heap Minimum"),minFreeHeapSeen/1024.0f,1,F(" KB"));
  webMetricCard(F("Reset"),String(resetReasonShort()));
  webMetricCard(F("Flash"),String(ESP.getFlashChipRealSize()/1024.0f/1024.0f,1)+F(" MB"));
  webMetricCard(F("Sketch"),String(ESP.getSketchSize()/1024.0f,1)+F(" KB"));
  webMetricCard(F("Sketch frei / OTA"),String(ESP.getFreeSketchSpace()/1024.0f,1)+F(" KB"));

  if(fsMounted&&LittleFS.info(fsInfoCache)){
    webMetricCard(F("LittleFS"),
      String(fsInfoCache.totalBytes/1024.0f,1)+F(" / ")+
      String(fsInfoCache.usedBytes/1024.0f,1)+F(" KB"));
  }else{
    webMetricCardFloat(F("LittleFS Gesamt"),fsInfoCache.totalBytes/1024.0f,1,F(" KB"));
    webMetricCardFloat(F("LittleFS Belegt"),fsInfoCache.usedBytes/1024.0f,1,F(" KB"));
    webMetricCardFloat(F("LittleFS Frei"),(fsInfoCache.totalBytes>fsInfoCache.usedBytes?(fsInfoCache.totalBytes-fsInfoCache.usedBytes):0)/1024.0f,1,F(" KB"));
  }

  webMetricCard(F("WLAN"),
    WiFi.status()==WL_CONNECTED?String(F("Verbunden")):String(F("Offline")));
  webMetricCard(F("MQTT"),
    mqttClient.connected()?String(F("Verbunden")):String(F("Offline")));
  webMetricCard(F("ToF"),
    String(sensorName(activeSensorType))+
    (sensorOk?String(F(" / OK")):String(F(" / Fehler"))));
  if(!cfg.ahtEnabled){
    webMetricCard(F("AHT10"),String(F("deaktiviert")));
  }else if(!ahtOk){
    webMetricCard(F("AHT10"),String(F("nicht erkannt / "))+String(ahtStatusText()));
    webMetricCard(F("AHT Diagnose"),
      String(F("0x38 · Retry "))+String(AHT_RETRY_MS/1000UL)+F(" s · Probes ")+String(ahtProbeCount)+
      F(" · Fehler ")+String(ahtErrorCount));
  }else{
    webMetricCard(F("AHT10"),String(F("OK @ 0x38")));
    webMetricCard(F("Temperatur"),String(ahtTemperatureC,1)+F(" °C"));
    webMetricCard(F("Luftfeuchte"),String(ahtHumidityPercent,1)+F(" %"));
    webMetricCard(F("Taupunkt"),String(ahtDewPointC,1)+F(" °C"));
    webMetricCard(F("Kondensationsreserve"),String(ahtCondensationReserveC,1)+F(" °C"));
    webMetricCard(F("AHT Status"),String(ahtStatusText()));
    webMetricCard(F("AHT Intervall"),String(cfg.ahtIntervalMs/1000UL)+F(" s"));
    webMetricCard(F("AHT Temp Offset"),String(cfg.ahtTemperatureOffsetC,1)+F(" °C"));
    webMetricCard(F("AHT RH Offset"),String(cfg.ahtHumidityOffsetPercent,1)+F(" %"));
    webMetricCard(F("AHT Alter"),lastAhtValidMs?String((millis()-lastAhtValidMs)/1000UL)+F(" s"):String(F("--")));
    webMetricCard(F("AHT Fehler"),String(ahtErrorCount));
    webMetricCard(F("AHT Recoveries"),String(ahtRecoveryCount));
  }
  webMetricCardUInt(F("Historie"),historyHeader.count,F(" Tage"));
  webMetricCardUInt(F("History Recordgröße"),sizeof(DailyHistoryRecord),F(" B/Tag"));
  webMetricCardUInt(F("History Duplikate"),historyRepairDuplicates);
  webMetricCardUInt(F("History CRC/invalid"),historyRepairInvalid);
  webMetricCard(F("History repariert"),historyRepairPerformed?String(F("JA")):String(F("NEIN")));
  webMetricCard(F("OTA"),String(F("Web OTA bereit")));
  webMetricCardUInt(F("WLAN-Reconnects"),wifiReconnectCount);
  webMetricCard(F("Messungen / Fehler"),
    String(measurementCount)+F(" / ")+String(measurementErrors));
  webMetricCard(F("History API"),
    String(historyApiRequests)+F(" Aufrufe / ")+
    String(historyApiErrors)+F(" Fehler"));
  webMetricCard(F("History API zuletzt"),
    String(historyApiLastItems)+F(" Punkte / ")+
    String(historyApiLastMs)+F(" ms"));
  webMetricCardUInt(F("Nachfüllschwelle"),HISTORY_REFILL_MIN_LITERS,F(" L"));
  webMetricCardFloat(F("Max Block frei"),ESP.getMaxFreeBlockSize()/1024.0f,1,F(" KB"));
  webMetricCardFloat(F("Max Block Minimum"),(lowestMaxBlockSeen?lowestMaxBlockSeen:ESP.getMaxFreeBlockSize())/1024.0f,1,F(" KB"));
  webMetricCardUInt(F("Heap Fragmentierung"),ESP.getHeapFragmentation(),F(" %"));
  webMetricCard(F("Fragmentierung Maximum"),
    String(highestHeapFragSeen)+F(" %"));
  webMetricCard(F("Web Requests"),String(webRequestCount));
  webMetricCard(F("Low-Heap Events"),String(webLowHeapEvents));
  webMetricCard(F("Web OTA Versuche"),String(webOtaAttempts));
  webMetricCard(F("Web OTA OK / Fehler"),
    String(webOtaSuccessCount)+F(" / ")+String(webOtaErrorCount));

  webMetricCard(F("Display Auto"),
    cfg.displayAutoRotate?String(F("AN")):String(F("AUS")));
  webMetricCard(F("Display Intervall"),
    String(cfg.displayPageSeconds)+F(" s"));
  webMetricCard(F("Display invertiert"),
    cfg.displayInvert?String(F("AN")):String(F("AUS")));
  webMetricCard(F("Display Schrift"),
    cfg.displayFontWeight==0?String(F("Normal")):
    (cfg.displayFontWeight==1?String(F("Fett")):String(F("Extra-Fett"))));

  server.sendContent(F("</div></div>"));
  webStreamEnd();

  const uint32_t heapAfter=ESP.getFreeHeap();
  Serial.print(F("[WEB] /systemstatus heap after="));
  Serial.print(heapAfter);
  Serial.print(F(" delta="));
  Serial.println((int32_t)heapAfter-(int32_t)heapBefore);
}

uint32_t storageRemoveKnownTempFile(const char* path){
  if(!fsMounted || !path || !path[0] || !LittleFS.exists(path))return 0;

  uint32_t size=0;
  File f=LittleFS.open(path,"r");
  if(f){
    size=(uint32_t)f.size();
    f.close();
  }

  if(LittleFS.remove(path)){
    Serial.print(F("[STORAGE CLEAN] geloescht "));
    Serial.print(path);
    Serial.print(F(" "));
    Serial.print(size);
    Serial.println(F(" B"));
    return size;
  }

  Serial.print(F("[STORAGE CLEAN] FEHLER "));
  Serial.println(path);
  return 0;
}

void handleStorageCleanupTemp(){
  if(!fsMounted){
    server.send(503,"text/plain; charset=utf-8","LittleFS nicht verfuegbar");
    return;
  }

  static const char* disposableFiles[]={
    HISTORY_IMPORT_PREVIEW_FILE,
    HISTORY_IMPORT_INDEX_FILE,
    HISTORY_REPAIR_INDEX_FILE,
    HISTORY_REPAIR_TMP_FILE,
    HISTORY_REPAIR_BAK_FILE,
    HISTORY_FILTER_TMP_FILE,
    HISTORY_FILTER_BAK_FILE,
    HISTORY_COMPACT_TMP_FILE,
    HISTORY_COMPACT_BAK_FILE,
    HISTORY_V2_MIGRATE_TMP_FILE,
    HISTORY_V2_MIGRATE_BAK_FILE
  };

  uint32_t reclaimed=0;
  uint8_t removed=0;

  Serial.println(F("[STORAGE CLEAN] Start"));

  for(uint8_t i=0;i<sizeof(disposableFiles)/sizeof(disposableFiles[0]);i++){
    const bool existed=LittleFS.exists(disposableFiles[i]);
    const uint32_t freed=storageRemoveKnownTempFile(disposableFiles[i]);
    if(existed && !LittleFS.exists(disposableFiles[i])){
      removed++;
      reclaimed+=freed;
    }
    yield();
  }

  if(LittleFS.info(fsInfoCache)){
    Serial.print(F("[STORAGE CLEAN] fertig files="));
    Serial.print(removed);
    Serial.print(F(" reclaimed="));
    Serial.print(reclaimed);
    Serial.print(F(" B free="));
    Serial.println(fsInfoCache.totalBytes>fsInfoCache.usedBytes
      ?fsInfoCache.totalBytes-fsInfoCache.usedBytes:0);
  }

  String html;
  html.reserve(480);
  html+=F("<!doctype html><html><head><meta charset='utf-8'>"
          "<meta name='viewport' content='width=device-width,initial-scale=1'>"
          "<title>Speicher aufgeräumt</title></head><body style='font-family:Arial;background:#111;color:#eee;padding:24px'>"
          "<h1>LittleFS aufgeräumt</h1><p>Gelöschte Temp-/Backup-Dateien: <b>");
  html+=String(removed);
  html+=F("</b></p><p>Freigegeben: <b>");
  html+=String(reclaimed/1024.0f,1);
  html+=F(" KB</b></p><p><a style='color:#69a7ff' href='/storage'>Zurück zu Speicher</a></p></body></html>");
  server.send(200,"text/html; charset=utf-8",html);
}

void handleStorageStatus() {
  const uint32_t heapBefore=ESP.getFreeHeap();
  Serial.print(F("[WEB] /storage heap before="));
  Serial.println(heapBefore);

  webStreamBegin(F("Speicher"));
  webStreamNav(3);

  server.sendContent(F(
    "<div class='card'><div class='topbar'><h1>Speicher & Historienplanung</h1>"
    "<div class='links'><a class='btn' href='/systemstatus'>System</a></div>"
    "</div><table>"
  ));

  webTableRow(F("Flash real"),String(ESP.getFlashChipRealSize())+F(" B"));
  webTableRow(F("Sketch"),String(ESP.getSketchSize())+F(" B"));
  webTableRow(F("FreeSketchSpace"),String(ESP.getFreeSketchSpace())+F(" B"));
  webTableRow(F("LittleFS"),
    fsMounted?String(F("gemountet")):String(F("nicht verfügbar")));

  uint32_t freeB=0;
  if(fsMounted&&LittleFS.info(fsInfoCache)){
    freeB=fsInfoCache.totalBytes>fsInfoCache.usedBytes
      ?fsInfoCache.totalBytes-fsInfoCache.usedBytes:0;

    webTableRow(F("LittleFS gesamt"),
      String(fsInfoCache.totalBytes)+F(" B"));
    webTableRow(F("LittleFS benutzt"),
      String(fsInfoCache.usedBytes)+F(" B"));
    webTableRow(F("LittleFS frei"),
      String(freeB)+F(" B"));
    webTableRow(F("History Record"),
      String(sizeof(DailyHistoryRecord))+F(" B"));
    webTableRow(F("History Einträge"),
      String(historyHeader.count));
    webTableRow(F("History Kapazität"),
      String(historyHeader.capacity)+F(" Tage"));
  }

  server.sendContent(F("</table></div>"));

  if(freeB>0){
    server.sendContent(F(
      "<div class='card'><h2>Mögliche Tageshistorie</h2><table>"
    ));

    const uint8_t sizes[]={16,20,24,32};
    for(uint8_t i=0;i<4;i++){
      const uint32_t recs=freeB/sizes[i];
      String label=String(sizes[i])+F(" Byte/Tag");
      if(sizes[i]==sizeof(DailyHistoryRecord))label+=F(" (aktuell)");
      String value=String(recs)+F(" Tage / ca. ")+
                   String((float)recs/365.25f,1)+F(" Jahre");

      if(sizes[i]==sizeof(DailyHistoryRecord)){
        server.sendContent(F("<tr style='font-weight:700;color:#65e572'><td>"));
      }else{
        server.sendContent(F("<tr><td>"));
      }

      webSendSafe(label);
      server.sendContent(F("</td><td>"));
      webSendSafe(value);
      server.sendContent(F("</td></tr>"));
      yield();
    }

    server.sendContent(F(
      "</table><p class='muted'>Historie verwendet aktuell "
    ));
    server.sendContent(String(sizeof(DailyHistoryRecord)));
    server.sendContent(F(
      " Byte pro Tag (History V3 mit Klima). Für LittleFS wird zusätzlich Reserve für Konfiguration, "
      "Import und temporäre Dateien benötigt.</p></div>"
    ));
  }

  if(fsMounted){
    server.sendContent(F(
      "<div class='card'><h2>LittleFS-Dateien</h2>"
      "<p class='muted'>Damit ist direkt sichtbar, welche Datei den Flash belegt.</p>"
      "<div style='overflow-x:auto'><table><tr><th>Datei</th><th>Größe</th><th>Anteil</th></tr>"
    ));

    Dir dir=LittleFS.openDir("/");
    uint32_t listedBytes=0;
    uint16_t fileCount=0;

    while(dir.next()){
      const String name=dir.fileName();
      const uint32_t size=(uint32_t)dir.fileSize();
      listedBytes+=size;
      fileCount++;

      server.sendContent(F("<tr><td><code>"));
      webSendSafe(name);
      server.sendContent(F("</code></td><td>"));
      webSendSafe(String(size)+F(" B / ")+String(size/1024.0f,1)+F(" KB"));
      server.sendContent(F("</td><td>"));

      float pct=0.0f;
      if(fsInfoCache.totalBytes>0)pct=(100.0f*(float)size)/(float)fsInfoCache.totalBytes;
      webSendSafe(String(pct,1)+F(" %"));
      server.sendContent(F("</td></tr>"));
      yield();
    }

    server.sendContent(F("</table></div><p class='muted'>"));
    webSendSafe(String(fileCount));
    server.sendContent(F(" Dateien aufgelistet · Dateisumme "));
    webSendSafe(String(listedBytes/1024.0f,1));
    server.sendContent(F(" KB. Die LittleFS-Belegung kann zusätzlich Dateisystem-Overhead enthalten.</p>"));

    server.sendContent(F(
      "<div class='topbar' style='margin-top:14px;gap:10px;flex-wrap:wrap'>"
      "<form method='POST' action='/storage/cleanup-temp' "
      "onsubmit=\"return confirm('Bekannte Temp-, Index- und Backup-Dateien löschen? Die aktive History und die hochgeladene Importdatei bleiben erhalten.');\">"
      "<button type='submit'>Temp-/Backup-Dateien aufräumen</button></form>"
      "<form method='POST' action='/history/maintenance/compact' "
      "onsubmit=\"return confirm('History kompakt neu schreiben und direkt aufeinanderfolgende Duplikate entfernen?');\">"
      "<button type='submit'>History kompakt neu schreiben</button></form>"
      "<a class='btn' href='/history/maintenance'>History-Wartung</a>"
      "</div>"
      "<p class='muted' style='margin-top:12px'>"
      "Der Aufräum-Button löscht niemals <code>/history.bin</code> und niemals <code>/history_import.csv</code>. "
      "Entfernt werden nur bekannte temporäre Index-, Repair-, Filter- und Compact-Dateien.</p>"
      "</div>"
    ));
  }

  webStreamEnd();

  const uint32_t heapAfter=ESP.getFreeHeap();
  Serial.print(F("[WEB] /storage heap after="));
  Serial.print(heapAfter);
  Serial.print(F(" delta="));
  Serial.println((int32_t)heapAfter-(int32_t)heapBefore);
}

void handleFactoryReset() {
  setDefaults();
  saveConfig();

  server.send(
    200,
    "text/html; charset=utf-8",
    F("<html><body><h1>Werkseinstellungen geladen</h1>"
      "<p>Das Geraet startet neu.</p></body></html>")
  );

  delay(500);
  ESP.restart();
}

void handleDisplayPageApi() {
  if(!server.hasArg("page")){
    server.send(400,"application/json; charset=utf-8","{\"ok\":false,\"error\":\"page_missing\"}");
    return;
  }

  int page=server.arg("page").toInt();
  if(page<0 || page>4){
    server.send(400,"application/json; charset=utf-8","{\"ok\":false,\"error\":\"page_out_of_range\"}");
    return;
  }

  displayPage=(uint8_t)page;
  lastDisplayPageMs=millis();
  drawDisplay();

  Serial.print(F("[LCD] manuelle Seite "));
  Serial.print(displayPage+1);
  Serial.print(F("/5 "));
  switch(displayPage){
    case 0: Serial.println(F("Fuellstand")); break;
    case 1: Serial.println(F("Sensor")); break;
    case 2: Serial.println(F("Netzwerk")); break;
    case 3: Serial.println(F("System")); break;
    default: Serial.println(F("Klima")); break;
  }

  String s=F("{\"ok\":true,\"page\":");
  s+=String(displayPage);
  s+=F(",\"auto\":");
  s+=cfg.displayAutoRotate?F("true"):F("false");
  s+=F(",\"seconds\":");
  s+=String(cfg.displayPageSeconds);
  s+=F("}");
  server.send(200,"application/json; charset=utf-8",s);
}

void setupWeb() {
  server.on("/", HTTP_GET, [](){
    Serial.print(F("[WEB] / heap before=")); Serial.println(ESP.getFreeHeap());
    handleRoot();
    Serial.print(F("[WEB] / heap after=")); Serial.println(ESP.getFreeHeap());
  });
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/display/page", HTTP_POST, handleDisplayPageApi);
  server.on("/api/health", HTTP_GET, handleHealthApi);
  server.on("/settings", HTTP_GET, handleSettings);
  server.on("/systemstatus", HTTP_GET, handleSystemStatusPage);
  server.on("/update", HTTP_GET, handleWebOtaPage);
  server.on("/update", HTTP_POST, handleWebOtaDone, handleWebOtaUpload);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/factory-reset", HTTP_GET, handleFactoryReset);
  server.on("/storage", HTTP_GET, handleStorageStatus);
  server.on("/storage/cleanup-temp", HTTP_POST, handleStorageCleanupTemp);
  server.on("/history", HTTP_GET, [](){
    Serial.print(F("[WEB] /history heap before=")); Serial.println(ESP.getFreeHeap());
    handleHistoryPage();
    Serial.print(F("[WEB] /history heap after=")); Serial.println(ESP.getFreeHeap());
  });
  server.on("/api/history", HTTP_GET, handleHistoryApi);
  server.on("/api/history/climate", HTTP_GET, handleClimateHistoryApi);
  server.on("/api/monthly-comparison", HTTP_GET, handleMonthlyComparisonApi);
  server.on("/api/recent-refills", HTTP_GET, handleRecentRefills);
  server.on("/history.csv", HTTP_GET, handleHistoryCsv);
  server.on("/history/import", HTTP_GET, handleHistoryImportPage);
  server.on("/history/import/preview", HTTP_POST, handleHistoryImportPreview, handleHistoryImportUpload);
  server.on("/history/import/apply", HTTP_POST, handleHistoryImportApply);
  server.on("/history/import/cancel", HTTP_POST, handleHistoryImportCancel);
  server.on("/history/maintenance", HTTP_GET, handleHistoryMaintenancePage);
  server.on("/history/maintenance/compact", HTTP_POST, handleHistoryCompactDuplicates);
  server.on("/history/maintenance/repair", HTTP_POST, handleHistoryMaintenanceRepair);
  server.on("/history/maintenance/delete-test", HTTP_POST, handleHistoryDeleteTestData);
  server.on("/history/maintenance/delete-imported", HTTP_POST, handleHistoryDeleteImportedData);
  server.on("/generate-test-history", HTTP_POST, handleGenerateTestHistory);
  server.on("/generate-test-history-10y", HTTP_POST, handleGenerate10YearTestHistory);
  server.on("/clear-history", HTTP_POST, handleClearHistory);

  server.on("/reboot", HTTP_GET, []() {
    server.send(200, "text/plain", "Reboot");
    delay(200);
    ESP.restart();
  });

  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println(F("[WEB] gestartet"));
  Serial.println(F("[OTA] ArduinoOTA entfernt - Web OTA bleibt aktiv"));
  Serial.println(F("[WEB] Safe-Chunk fuer leere Konfigurationswerte aktiv"));
  Serial.println(F("[WEB] HTTP KeepAlive AUS / Connection close aktiv"));
  Serial.println(F("[RAM] String-Free History/CSV/Web Helfer aktiv"));
  Serial.println(F("[UI] Klima in Dashboard-/Historiegrafik integriert"));
  Serial.println(F("[FIX] History JS Loader + Tooltip wiederhergestellt"));
  Serial.println(F("[SYSTEM] LittleFS Gesamt/Belegt/Frei + beruhigte Heap-Diagnose"));
  Serial.println(F("[UI] Dashboard/Historie kompakt + Klima-Schalter dynamisch"));
  Serial.println(F("[FIX] Web-Number-Helper Prototypen vor History API"));
  Serial.println(F("[HA] Discovery ENTFERNT - MQTT Topics bleiben aktiv"));
  Serial.print(F("[MQTT] Buffer="));Serial.println(MQTT_BUFFER_NORMAL);
}

