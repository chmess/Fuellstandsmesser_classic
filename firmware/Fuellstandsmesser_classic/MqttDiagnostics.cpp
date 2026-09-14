#include "MqttDiagnostics.h"
#include "AppConstants.h"
#include "AppTypes.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <LittleFS.h>
#include <FS.h>

extern Config cfg;

extern WiFiClient wifiClient;
extern PubSubClient mqttClient;

extern bool apMode;
extern bool sensorOk;
extern uint8_t activeSensorType;

extern uint16_t rawDistanceMm;
extern uint16_t filteredDistanceMm;
extern float measuredLiters;
extern float measuredPercent;
extern bool measurementValid;

extern bool ahtOk;
extern float ahtTemperatureC;
extern float ahtHumidityPercent;
extern float ahtDewPointC;
extern float ahtCondensationReserveC;

extern uint32_t minFreeHeapSeen;
extern uint32_t wifiReconnectCount;
extern uint32_t wifiReconnectErrors;

extern bool fsMounted;

// Shared application state owned by the main sketch / remaining INO modules.
extern float tankLiters;
extern float tankPercent;
extern float tankHeightNowMm;

extern uint32_t mqttPublishCount;
extern uint32_t mqttPublishErrors;
extern bool mqttLastDnsOk;
extern IPAddress mqttLastResolvedIp;
extern bool mqttLastTcpOk;
extern uint32_t mqttConnectCount;
extern uint32_t mqttConnectErrors;
extern uint32_t mqttRetryIntervalMs;
extern uint32_t mqttLastConnectDurationMs;

extern uint32_t lowestMaxBlockSeen;
extern uint8_t highestHeapFragSeen;
extern FSInfo fsInfoCache;


// Cross-module functions still implemented outside this .cpp.
const char* sensorName(uint8_t sensorType);
float historyConsumptionDays(uint16_t days);

// -----------------------------------------------------------------------------
void publishMqtt() {
  if (!cfg.mqttEnabled || !mqttClient.connected()) return;
  if (!isfinite(tankLiters) || !isfinite(filteredDistanceMm)) return;

  char buf[32];
  char topic[112];

  auto publishBase = [&](const char* suffix,const char* value)->bool {
    const int n=snprintf(topic,sizeof(topic),"%s/%s",cfg.mqttBase,suffix);
    if(n<=0 || (size_t)n>=sizeof(topic)){
      mqttPublishErrors++;
      Serial.print(F("[MQTT] Topic zu lang: "));
      Serial.println(suffix);
      return false;
    }
    const bool ok=mqttClient.publish(topic,value,true);
    if(ok)mqttPublishCount++;else mqttPublishErrors++;
    return ok;
  };

  // Fuellstandsmesser3-Kompatibilitaet unveraendert.
  dtostrf(tankLiters,0,1,buf);
  const bool okAverage=mqttClient.publish(cfg.mqttAverageTopic,buf,true);
  if(okAverage)mqttPublishCount++;else mqttPublishErrors++;

  Serial.print(F("[MQTT] average "));
  Serial.print(cfg.mqttAverageTopic);
  Serial.print(F(" = "));
  Serial.print(buf);
  Serial.println(okAverage?F(" OK"):F(" FEHLER"));

  dtostrf(filteredDistanceMm,0,1,buf);
  const bool okFuellhoehe=mqttClient.publish(cfg.mqttFuellhoeheTopic,buf,true);
  if(okFuellhoehe)mqttPublishCount++;else mqttPublishErrors++;

  Serial.print(F("[MQTT] fuellhoehe "));
  Serial.print(cfg.mqttFuellhoeheTopic);
  Serial.print(F(" = "));
  Serial.print(buf);
  Serial.println(okFuellhoehe?F(" OK"):F(" FEHLER"));

  if(isfinite(tankPercent)){
    dtostrf(tankPercent,0,1,buf);
    publishBase("percent",buf);
  }

  if(isfinite(filteredDistanceMm)){
    dtostrf(filteredDistanceMm,0,1,buf);
    publishBase("distance_mm",buf);
  }

  if(isfinite(tankHeightNowMm)){
    dtostrf(tankHeightNowMm,0,1,buf);
    publishBase("level_height_mm",buf);
  }

  if(isfinite(tankLiters)){
    dtostrf(tankLiters,0,1,buf);
    publishBase("liters",buf);
  }

  publishBase("sensor",sensorName(activeSensorType));

  // History-Statistikcache verhindert wiederholtes komplettes Einlesen.
  const float cToday=historyConsumptionDays(1);
  const float c7=historyConsumptionDays(7);
  const float c30=historyConsumptionDays(30);

  dtostrf(cToday,0,1,buf);
  publishBase("consumption_today_l",buf);

  dtostrf(c7,0,1,buf);
  publishBase("consumption_7d_l",buf);

  dtostrf(c30,0,1,buf);
  publishBase("consumption_30d_l",buf);

  dtostrf(c30/30.0f,0,1,buf);
  publishBase("consumption_avg_30d_l_day",buf);

  if(WiFi.status()==WL_CONNECTED){
    ltoa(WiFi.RSSI(),buf,10);
    publishBase("wifi_rssi",buf);
  }

  if(ahtOk){
    dtostrf(ahtTemperatureC,0,1,buf);
    publishBase("temperature_c",buf);

    dtostrf(ahtHumidityPercent,0,1,buf);
    publishBase("humidity_percent",buf);

    dtostrf(ahtDewPointC,0,1,buf);
    publishBase("dew_point_c",buf);

    dtostrf(ahtCondensationReserveC,0,1,buf);
    publishBase("condensation_reserve_c",buf);
  }

  Serial.print(F("[MQTT] publish gesamt OK="));
  Serial.print(mqttPublishCount);
  Serial.print(F(" Fehler="));
  Serial.println(mqttPublishErrors);
}

bool mqttResolveBroker(IPAddress& ip){
  mqttLastDnsOk=false;
  ip=IPAddress();

  if(strlen(cfg.mqttHost)==0)return false;

  if(ip.fromString(cfg.mqttHost)){
    mqttLastDnsOk=true;
    mqttLastResolvedIp=ip;
    Serial.print(F("[MQTT DIAG] Broker ist IP: "));
    Serial.println(ip);
    return true;
  }

  const uint32_t t0=millis();
  const int ok=WiFi.hostByName(cfg.mqttHost,ip);
  const uint32_t dt=millis()-t0;

  Serial.print(F("[MQTT DIAG] DNS host="));
  Serial.print(cfg.mqttHost);
  Serial.print(F(" result="));
  Serial.print(ok==1?F("OK"):F("FEHLER"));
  Serial.print(F(" time="));
  Serial.print(dt);
  Serial.print(F(" ms"));

  if(ok==1){
    mqttLastDnsOk=true;
    mqttLastResolvedIp=ip;
    Serial.print(F(" ip="));
    Serial.print(ip);
  }
  Serial.println();

  return ok==1;
}

bool mqttTcpProbe(const IPAddress& ip){
  mqttLastTcpOk=false;
  WiFiClient probe;
  probe.setTimeout(1500);

  const uint32_t t0=millis();
  const bool ok=probe.connect(ip,cfg.mqttPort);
  const uint32_t dt=millis()-t0;

  Serial.print(F("[MQTT DIAG] TCP "));
  Serial.print(ip);
  Serial.print(':');
  Serial.print(cfg.mqttPort);
  Serial.print(F(" -> "));
  Serial.print(ok?F("OK"):F("FEHLER"));
  Serial.print(F(" time="));
  Serial.print(dt);
  Serial.println(F(" ms"));

  if(ok)probe.stop();
  mqttLastTcpOk=ok;
  return ok;
}

void mqttPrintStatus(){
  Serial.println(F("[MQTT STATUS]"));
  Serial.print(F("  aktiviert   : "));
  Serial.println(cfg.mqttEnabled?F("JA"):F("NEIN"));
  Serial.print(F("  broker      : "));
  Serial.print(cfg.mqttHost);
  Serial.print(':');
  Serial.println(cfg.mqttPort);
  Serial.print(F("  verbunden   : "));
  Serial.println(mqttClient.connected()?F("JA"):F("NEIN"));
  Serial.print(F("  state       : "));
  Serial.println(mqttClient.state());
  Serial.print(F("  DNS zuletzt : "));
  if(mqttLastDnsOk)Serial.println(mqttLastResolvedIp);
  else Serial.println(F("FEHLER / nicht getestet"));
  Serial.print(F("  TCP zuletzt : "));
  Serial.println(mqttLastTcpOk?F("OK"):F("FEHLER / nicht getestet"));
  Serial.print(F("  connect OK  : "));
  Serial.println(mqttConnectCount);
  Serial.print(F("  connect Err : "));
  Serial.println(mqttConnectErrors);
  Serial.print(F("  retry       : "));
  Serial.print(mqttRetryIntervalMs/1000UL);
  Serial.println(F(" s"));
  Serial.print(F("  last time   : "));
  Serial.print(mqttLastConnectDurationMs);
  Serial.println(F(" ms"));
}

void mqttDiagnosticTest(){
  Serial.println(F("[MQTT DIAG] Test Start"));

  if(WiFi.status()!=WL_CONNECTED){
    Serial.println(F("[MQTT DIAG] WLAN nicht verbunden"));
    return;
  }

  Serial.print(F("[MQTT DIAG] localIP="));
  Serial.print(WiFi.localIP());
  Serial.print(F(" gateway="));
  Serial.print(WiFi.gatewayIP());
  Serial.print(F(" dns="));
  Serial.println(WiFi.dnsIP());

  IPAddress ip;
  if(!mqttResolveBroker(ip)){
    Serial.println(F("[MQTT DIAG] Abbruch: Brokername nicht aufloesbar"));
    return;
  }

  mqttTcpProbe(ip);
}

bool connectMqtt() {
  mqttClient.setBufferSize(MQTT_BUFFER_NORMAL);
  if (!cfg.mqttEnabled) return false;
  if (WiFi.status() != WL_CONNECTED) return false;
  if (strlen(cfg.mqttHost) == 0) return false;

  const uint32_t t0=millis();

  IPAddress brokerIp;
  if(!mqttResolveBroker(brokerIp)){
    mqttConnectErrors++;
    mqttLastConnectDurationMs=millis()-t0;
    mqttRetryIntervalMs=min<uint32_t>(MQTT_RETRY_MAX_MS,mqttRetryIntervalMs*2UL);

    Serial.print(F("[MQTT] DNS FEHLER host="));
    Serial.print(cfg.mqttHost);
    Serial.print(F(" nextRetry="));
    Serial.print(mqttRetryIntervalMs/1000UL);
    Serial.println(F("s"));
    return false;
  }

  if(!mqttTcpProbe(brokerIp)){
    mqttConnectErrors++;
    mqttLastConnectDurationMs=millis()-t0;
    mqttRetryIntervalMs=min<uint32_t>(MQTT_RETRY_MAX_MS,mqttRetryIntervalMs*2UL);

    Serial.print(F("[MQTT] TCP FEHLER Broker="));
    Serial.print(cfg.mqttHost);
    Serial.print(':');
    Serial.print(cfg.mqttPort);
    Serial.print(F(" nextRetry="));
    Serial.print(mqttRetryIntervalMs/1000UL);
    Serial.println(F("s"));
    return false;
  }

  mqttClient.setServer(brokerIp, cfg.mqttPort);

  String clientId = "Fuellstandsmesser_classic-";
  clientId += String(ESP.getChipId(), HEX);

  String statusTopic = String(cfg.mqttBase) + "/status";

  bool ok;
  if (strlen(cfg.mqttUser) > 0) {
    ok = mqttClient.connect(
      clientId.c_str(),
      cfg.mqttUser,
      cfg.mqttPass,
      statusTopic.c_str(),
      0,
      true,
      "offline"
    );
  } else {
    ok = mqttClient.connect(
      clientId.c_str(),
      statusTopic.c_str(),
      0,
      true,
      "offline"
    );
  }

  mqttLastConnectDurationMs=millis()-t0;

  if (ok) {
    mqttConnectCount++;
    mqttRetryIntervalMs=MQTT_RETRY_MS;
    mqttClient.publish(statusTopic.c_str(), "online", true);

    Serial.print(F("[MQTT] verbunden Broker="));
    Serial.print(cfg.mqttHost);
    Serial.print(F(" -> "));
    Serial.print(brokerIp);
    Serial.print(':');
    Serial.print(cfg.mqttPort);
    Serial.print(F(" ClientID="));
    Serial.print(clientId);
    Serial.print(F(" time="));
    Serial.print(mqttLastConnectDurationMs);
    Serial.println(F(" ms"));

    Serial.print(F("[MQTT] Compat Topics: average='"));
    Serial.print(cfg.mqttAverageTopic);
    Serial.print(F("' fuellhoehe='"));
    Serial.print(cfg.mqttFuellhoeheTopic);
    Serial.println(F("'"));

    
    publishMqtt();
  } else {
    mqttConnectErrors++;
    mqttRetryIntervalMs=min<uint32_t>(MQTT_RETRY_MAX_MS,mqttRetryIntervalMs*2UL);

    Serial.print(F("[MQTT] Protokoll FEHLER rc="));
    Serial.print(mqttClient.state());
    Serial.print(F(" connectOK="));
    Serial.print(mqttConnectCount);
    Serial.print(F(" connectErr="));
    Serial.print(mqttConnectErrors);
    Serial.print(F(" time="));
    Serial.print(mqttLastConnectDurationMs);
    Serial.print(F(" ms nextRetry="));
    Serial.print(mqttRetryIntervalMs/1000UL);
    Serial.println(F("s"));
  }

  return ok;
}

const char* resetReasonShort() {
  const rst_info* ri = ESP.getResetInfoPtr();
  if (!ri) return "UNKNOWN";
  switch (ri->reason) {
    case REASON_DEFAULT_RST:      return "POWERON";
    case REASON_WDT_RST:          return "HW_WDT";
    case REASON_EXCEPTION_RST:    return "EXCEPTION";
    case REASON_SOFT_WDT_RST:     return "SOFT_WDT";
    case REASON_SOFT_RESTART:     return "SW_RESTART";
    case REASON_DEEP_SLEEP_AWAKE: return "DEEPSLEEP";
    case REASON_EXT_SYS_RST:      return "EXT_RESET";
    default:                      return "OTHER";
  }
}

void printBootDiagnostics() {
  Serial.print(F("[BOOT] Reset="));
  Serial.print(resetReasonShort());
  Serial.print(F(" | SDK="));
  Serial.print(ESP.getSdkVersion());
  Serial.print(F(" | Flash="));
  Serial.print(ESP.getFlashChipRealSize() / 1024UL);
  Serial.print(F(" KiB | CPU="));
  Serial.print(ESP.getCpuFreqMHz());
  Serial.println(F(" MHz"));

  Serial.print(F("[HEAP] free="));
  Serial.print(ESP.getFreeHeap());
  Serial.print(F(" maxBlock="));
  Serial.print(ESP.getMaxFreeBlockSize());
  Serial.print(F(" frag="));
  Serial.print(ESP.getHeapFragmentation());
  Serial.println('%');
}

void updateHeapDiag() {
  const uint32_t h = ESP.getFreeHeap();
  const uint32_t block = ESP.getMaxFreeBlockSize();
  const uint8_t frag = ESP.getHeapFragmentation();

  if (h < minFreeHeapSeen) minFreeHeapSeen = h;
  if (lowestMaxBlockSeen == 0 || block < lowestMaxBlockSeen) lowestMaxBlockSeen = block;
  if (frag > highestHeapFragSeen) highestHeapFragSeen = frag;
}

void printStorageDiagnostics() {
  uint32_t flashReal = ESP.getFlashChipRealSize();
  uint32_t flashIde  = ESP.getFlashChipSize();
  uint32_t sketchSize = ESP.getSketchSize();
  uint32_t freeSketch = ESP.getFreeSketchSpace();

  Serial.println(F("[STORAGE] ------------------------------"));
  Serial.print(F("[STORAGE] Flash real="));
  Serial.print(flashReal);
  Serial.print(F(" B ("));
  Serial.print(flashReal / 1024UL);
  Serial.println(F(" KiB)"));

  Serial.print(F("[STORAGE] Flash IDE ="));
  Serial.print(flashIde);
  Serial.print(F(" B ("));
  Serial.print(flashIde / 1024UL);
  Serial.println(F(" KiB)"));

  Serial.print(F("[STORAGE] Sketch="));
  Serial.print(sketchSize);
  Serial.print(F(" B | FreeSketchSpace="));
  Serial.print(freeSketch);
  Serial.println(F(" B"));

  if (fsMounted && LittleFS.info(fsInfoCache)) {
    uint32_t total = fsInfoCache.totalBytes;
    uint32_t used  = fsInfoCache.usedBytes;
    uint32_t freeB = total > used ? total - used : 0;

    Serial.print(F("[STORAGE] LittleFS total="));
    Serial.print(total);
    Serial.print(F(" B used="));
    Serial.print(used);
    Serial.print(F(" B free="));
    Serial.print(freeB);
    Serial.println(F(" B"));

    Serial.print(F("[STORAGE] Tagesrecords bei 16B="));
    Serial.print(freeB / 16UL);
    Serial.print(F(" (~"));
    Serial.print((freeB / 16UL) / 365UL);
    Serial.println(F(" Jahre)"));

    Serial.print(F("[STORAGE] Tagesrecords bei 20B="));
    Serial.print(freeB / 20UL);
    Serial.print(F(" (~"));
    Serial.print((freeB / 20UL) / 365UL);
    Serial.println(F(" Jahre)"));

    Serial.print(F("[STORAGE] Tagesrecords bei 24B="));
    Serial.print(freeB / 24UL);
    Serial.print(F(" (~"));
    Serial.print((freeB / 24UL) / 365UL);
    Serial.println(F(" Jahre)"));
  } else {
    Serial.println(F("[STORAGE] LittleFS nicht verfuegbar/gemountet"));
  }

  if(fsInfoCache.totalBytes>HISTORY_RESERVE_BYTES){
    const uint32_t usable=fsInfoCache.totalBytes-HISTORY_RESERVE_BYTES;
    const uint32_t r32=usable/32UL;
    Serial.print(F("[STORAGE] Tagesrecords bei 32B="));
    Serial.print(r32);
    Serial.print(F(" (~"));
    Serial.print((float)r32/365.25f,0);
    Serial.println(F(" Jahre)"));
  }
  Serial.println(F("[STORAGE] ------------------------------"));
}
