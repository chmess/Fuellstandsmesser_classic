#include "History.h"
#include "AppConstants.h"
#include "ConfigI2C.h"
#include "Measurement.h"
#include "MqttDiagnostics.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <FS.h>
#include <ESP8266WebServer.h>
#include <time.h>
#include <math.h>
#include <stddef.h>

// -----------------------------------------------------------------------------
// Module-owned History constants
// -----------------------------------------------------------------------------
extern const uint32_t HISTORY_MAGIC = 0x48495332UL;
extern const uint16_t HISTORY_VERSION = 3;
extern const uint16_t HISTORY_VERSION_V2 = 2;
const char* HISTORY_FILE = "/history.bin";
const char* HISTORY_IMPORT_FILE = "/history_import.csv";
const char* HISTORY_IMPORT_PREVIEW_FILE = "/history_import_preview.csv";
const char* HISTORY_IMPORT_INDEX_FILE = "/history_import.idx";
const char* HISTORY_REPAIR_INDEX_FILE = "/history_repair.idx";
const char* HISTORY_REPAIR_TMP_FILE = "/history_repair.tmp";
const char* HISTORY_REPAIR_BAK_FILE = "/history_repair.bak";
const char* HISTORY_FILTER_TMP_FILE = "/history_filter.tmp";
const char* HISTORY_FILTER_BAK_FILE = "/history_filter.bak";
const char* HISTORY_COMPACT_TMP_FILE = "/history_compact.tmp";
const char* HISTORY_COMPACT_BAK_FILE = "/history_compact.bak";
const char* HISTORY_V2_MIGRATE_TMP_FILE = "/history_v3_migrate.tmp";
const char* HISTORY_V2_MIGRATE_BAK_FILE = "/history_v2_backup.bak";
extern const uint32_t HISTORY_CHECKPOINT_MS = 12UL * 60UL * 60UL * 1000UL;
extern const uint16_t HISTORY_REFILL_MIN_LITERS = 150;
extern const uint8_t HISTORY_FLAG_PREV_DAY_BASELINE = 0x01;

// HISTORY_RESERVE_BYTES remains shared with MqttDiagnostics.cpp.

// -----------------------------------------------------------------------------
// Module-owned History runtime state
// -----------------------------------------------------------------------------
HistoryHeader historyHeader;
bool historyReady = false;
bool historyTimeValid = false;
DailyHistoryRecord historyCurrent;
bool historyCurrentValid = false;
uint32_t historyCurrentIndex = 0;
uint64_t historyPercentSum = 0;
uint32_t historyClimateSampleCount = 0;
uint16_t historyDayBaselineLiters = 0;
bool historyDayBaselineValid = false;
bool historyDayRefillConfirmed = false;
uint16_t historyDayMaxRiseLiters = 0;
uint32_t historyLastCheckpointMs = 0;
uint32_t historyWriteCount = 0;
uint32_t historyWriteErrors = 0;
uint32_t historyRepairDuplicates = 0;
uint32_t historyRepairInvalid = 0;
uint32_t historyRepairOutOfOrder = 0;
uint32_t historyRepairRemoved = 0;
bool historyRepairPerformed = false;
uint32_t historyCompactDuplicates = 0;
uint32_t historyCompactInvalid = 0;
bool historyCompactPerformed = false;
HistoryStatsCache historyStatsCache = {};
uint32_t historyApiRequests = 0;
uint32_t historyApiErrors = 0;
uint32_t historyApiLastMs = 0;
uint32_t historyApiLastItems = 0;

// -----------------------------------------------------------------------------
// Application state / APIs owned by other modules
// -----------------------------------------------------------------------------
extern bool fsMounted;
extern FSInfo fsInfoCache;
extern ESP8266WebServer server;

extern bool ahtOk;
extern float ahtTemperatureC;
extern float ahtHumidityPercent;
extern float tankLiters;
extern float tankPercent;

uint32_t crc32Bytes(const uint8_t* data, size_t len);
void printStorageDiagnostics();
float tankCapacityLiters();

// Web streaming helpers remain in 80_Web.ino until the Web module is migrated.
void webPrepareConnectionClose();
void webFinishConnection();
void webStreamBegin(const __FlashStringHelper* title);
void webStreamNav(uint8_t active);
void webStreamEnd();
void webMetricCard(const __FlashStringHelper* label,const String& value);
void webMetricCardUInt(const __FlashStringHelper* label,uint32_t value,const __FlashStringHelper* unit = nullptr);
void webSendSafe(const String& value);
void webSendSafe(const char* value);
void webSendUInt(uint32_t value);
void webSendFloat(float value,uint8_t decimals);

// These two functions intentionally remain in 80_Web.ino in Stage 5B because
// their implementation is tightly coupled to the web statistics cache view.
void historyBuildStatsCache();
float historyConsumptionDays(uint16_t days);

void setupFilesystem() {
  fsMounted = LittleFS.begin();

  if (fsMounted) {
    Serial.println(F("[FS] LittleFS gemountet"));
  } else {
    Serial.println(F("[FS] LittleFS Mount FEHLER - KEIN Autoformat"));
  }

  printStorageDiagnostics();
}


uint16_t historyCrc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  while (len--) {
    crc ^= (uint16_t)(*data++) << 8;
    for (uint8_t i=0;i<8;i++) crc=(crc&0x8000)?(uint16_t)((crc<<1)^0x1021):(uint16_t)(crc<<1);
  }
  return crc;
}

uint32_t historyHeaderCrc(const HistoryHeader& h) {
  return crc32Bytes(reinterpret_cast<const uint8_t*>(&h), offsetof(HistoryHeader, crc));
}

uint16_t historyRecordCrc(const DailyHistoryRecord& r) {
  return historyCrc16(reinterpret_cast<const uint8_t*>(&r), offsetof(DailyHistoryRecord, crc16));
}

uint16_t historyRecordCrcV2(const DailyHistoryRecordV2& r) {
  return historyCrc16(reinterpret_cast<const uint8_t*>(&r), offsetof(DailyHistoryRecordV2, crc16));
}

uint8_t historyEncodeTempHalfC(float c){
  if(!isfinite(c))return 255;
  c=constrain(c,-40.0f,85.0f);
  return (uint8_t)constrain((int)lroundf((c+40.0f)*2.0f),0,250);
}

float historyDecodeTempHalfC(uint8_t v){
  return v==255 ? NAN : ((float)v*0.5f-40.0f);
}

uint8_t historyEncodeHumidity(float rh){
  if(!isfinite(rh))return 255;
  return (uint8_t)constrain((int)lroundf(rh),0,100);
}

float historyDecodeHumidity(uint8_t v){
  return v==255 ? NAN : (float)v;
}

void historyClimateClear(DailyHistoryRecord& r){
  r.tempAvgHalfC=255;
  r.tempMinHalfC=255;
  r.tempMaxHalfC=255;
  r.humidityAvgPct=255;
  r.humidityMinPct=255;
  r.humidityMaxPct=255;
  r.climateSamples=0;
}

void historyClimateAccumulate(DailyHistoryRecord& r){
  if(!ahtOk || !isfinite(ahtTemperatureC) || !isfinite(ahtHumidityPercent))return;

  const float t=ahtTemperatureC;
  const float h=ahtHumidityPercent;

  if(r.climateSamples==0 || r.tempAvgHalfC==255 || r.humidityAvgPct==255){
    r.tempAvgHalfC=historyEncodeTempHalfC(t);
    r.tempMinHalfC=historyEncodeTempHalfC(t);
    r.tempMaxHalfC=historyEncodeTempHalfC(t);
    r.humidityAvgPct=historyEncodeHumidity(h);
    r.humidityMinPct=historyEncodeHumidity(h);
    r.humidityMaxPct=historyEncodeHumidity(h);
    r.climateSamples=1;
    return;
  }

  const uint32_t n=r.climateSamples;
  float tAvg=historyDecodeTempHalfC(r.tempAvgHalfC);
  float hAvg=historyDecodeHumidity(r.humidityAvgPct);
  if(!isfinite(tAvg))tAvg=t;
  if(!isfinite(hAvg))hAvg=h;

  tAvg=(tAvg*(float)n+t)/(float)(n+1U);
  hAvg=(hAvg*(float)n+h)/(float)(n+1U);

  const float oldTMin=historyDecodeTempHalfC(r.tempMinHalfC);
  const float oldTMax=historyDecodeTempHalfC(r.tempMaxHalfC);
  const float oldHMin=historyDecodeHumidity(r.humidityMinPct);
  const float oldHMax=historyDecodeHumidity(r.humidityMaxPct);

  r.tempAvgHalfC=historyEncodeTempHalfC(tAvg);
  r.tempMinHalfC=historyEncodeTempHalfC(isfinite(oldTMin)?min(oldTMin,t):t);
  r.tempMaxHalfC=historyEncodeTempHalfC(isfinite(oldTMax)?max(oldTMax,t):t);
  r.humidityAvgPct=historyEncodeHumidity(hAvg);
  r.humidityMinPct=historyEncodeHumidity(isfinite(oldHMin)?min(oldHMin,h):h);
  r.humidityMaxPct=historyEncodeHumidity(isfinite(oldHMax)?max(oldHMax,h):h);
  if(r.climateSamples<65535)r.climateSamples++;
}

bool historyMigrateV2ToV3(uint32_t newCapacity){
  if(!LittleFS.exists(HISTORY_FILE))return false;

  File src=LittleFS.open(HISTORY_FILE,"r");
  if(!src)return false;

  HistoryHeader oldHeader{};
  if(src.read(reinterpret_cast<uint8_t*>(&oldHeader),sizeof(oldHeader))!=sizeof(oldHeader)){
    src.close();return false;
  }

  const bool oldValid=
    oldHeader.magic==HISTORY_MAGIC &&
    oldHeader.version==HISTORY_VERSION_V2 &&
    oldHeader.recordSize==sizeof(DailyHistoryRecordV2) &&
    oldHeader.capacity>0 &&
    oldHeader.count<=oldHeader.capacity &&
    oldHeader.writeIndex<oldHeader.capacity &&
    oldHeader.crc==historyHeaderCrc(oldHeader);

  if(!oldValid){src.close();return false;}

  Serial.print(F("[HISTORY V3] Migration V2->V3 Start count="));
  Serial.println(oldHeader.count);

  LittleFS.remove(HISTORY_V2_MIGRATE_TMP_FILE);
  File tmp=LittleFS.open(HISTORY_V2_MIGRATE_TMP_FILE,"w+");
  if(!tmp){src.close();return false;}

  HistoryHeader newHeader{};
  newHeader.magic=HISTORY_MAGIC;
  newHeader.version=HISTORY_VERSION;
  newHeader.recordSize=sizeof(DailyHistoryRecord);
  newHeader.capacity=newCapacity;
  newHeader.count=0;
  newHeader.writeIndex=0;
  newHeader.crc=historyHeaderCrc(newHeader);

  if(tmp.write(reinterpret_cast<const uint8_t*>(&newHeader),sizeof(newHeader))!=sizeof(newHeader)){
    src.close();tmp.close();LittleFS.remove(HISTORY_V2_MIGRATE_TMP_FILE);return false;
  }

  uint32_t keep=min(oldHeader.count,newCapacity);
  uint32_t skip=oldHeader.count-keep;
  uint32_t oldOldest=(oldHeader.count<oldHeader.capacity)?0:oldHeader.writeIndex;
  uint32_t migrated=0,invalid=0;

  for(uint32_t li=skip;li<oldHeader.count;li++){
    const uint32_t physical=(oldOldest+li)%oldHeader.capacity;
    const uint32_t off=sizeof(HistoryHeader)+physical*sizeof(DailyHistoryRecordV2);
    DailyHistoryRecordV2 oldRec{};
    if(!src.seek(off,SeekSet) ||
       src.read(reinterpret_cast<uint8_t*>(&oldRec),sizeof(oldRec))!=sizeof(oldRec) ||
       !oldRec.dayKey || oldRec.crc16!=historyRecordCrcV2(oldRec)){
      invalid++;
      continue;
    }

    DailyHistoryRecord r{};
    r.dayKey=oldRec.dayKey;
    r.samples=oldRec.samples;
    r.levelLiters=oldRec.levelLiters;
    r.avgPermille=oldRec.avgPermille;
    r.minPermille=oldRec.minPermille;
    r.maxPermille=oldRec.maxPermille;
    r.firstLiters=oldRec.firstLiters;
    r.consumptionLiters=oldRec.consumptionLiters;
    r.refillLiters=oldRec.refillLiters;
    r.source=oldRec.source;
    r.flags=oldRec.flags;
    historyClimateClear(r);
    r.crc16=historyRecordCrc(r);

    if(tmp.write(reinterpret_cast<const uint8_t*>(&r),sizeof(r))!=sizeof(r)){
      src.close();tmp.close();LittleFS.remove(HISTORY_V2_MIGRATE_TMP_FILE);return false;
    }
    migrated++;
    if((migrated&0x7F)==0)yield();
  }

  newHeader.count=migrated;
  newHeader.writeIndex=(newCapacity>0)?(migrated%newCapacity):0;
  newHeader.crc=historyHeaderCrc(newHeader);

  if(!tmp.seek(0,SeekSet) ||
     tmp.write(reinterpret_cast<const uint8_t*>(&newHeader),sizeof(newHeader))!=sizeof(newHeader)){
    src.close();tmp.close();LittleFS.remove(HISTORY_V2_MIGRATE_TMP_FILE);return false;
  }

  tmp.flush();src.close();tmp.close();

  LittleFS.remove(HISTORY_V2_MIGRATE_BAK_FILE);
  if(!LittleFS.rename(HISTORY_FILE,HISTORY_V2_MIGRATE_BAK_FILE)){
    LittleFS.remove(HISTORY_V2_MIGRATE_TMP_FILE);
    return false;
  }
  if(!LittleFS.rename(HISTORY_V2_MIGRATE_TMP_FILE,HISTORY_FILE)){
    LittleFS.rename(HISTORY_V2_MIGRATE_BAK_FILE,HISTORY_FILE);
    LittleFS.remove(HISTORY_V2_MIGRATE_TMP_FILE);
    return false;
  }
  LittleFS.remove(HISTORY_V2_MIGRATE_BAK_FILE);

  Serial.print(F("[HISTORY V3] Migration fertig migrated="));
  Serial.print(migrated);
  Serial.print(F(" invalid="));
  Serial.println(invalid);
  return true;
}

bool historyDateNow(uint32_t& dayKey) {
  time_t now=time(nullptr);
  if(now<1700000000)return false;
  struct tm t; localtime_r(&now,&t);
  dayKey=(uint32_t)(t.tm_year+1900)*10000UL+(uint32_t)(t.tm_mon+1)*100UL+(uint32_t)t.tm_mday;
  return true;
}

String historyDateString(uint32_t dayKey) {
  char b[16];
  snprintf(b,sizeof(b),"%02u.%02u.%04u",(unsigned)(dayKey%100),(unsigned)((dayKey/100)%100),(unsigned)(dayKey/10000));
  return String(b);
}

uint32_t historyDayKeyFromTime(time_t ts) {
  struct tm t; localtime_r(&ts,&t);
  return (uint32_t)(t.tm_year+1900)*10000UL+(uint32_t)(t.tm_mon+1)*100UL+(uint32_t)t.tm_mday;
}

time_t historyDayKeyToTime(uint32_t k) {
  struct tm t={};
  t.tm_year=(int)(k/10000UL)-1900;
  t.tm_mon=(int)((k/100UL)%100UL)-1;
  t.tm_mday=(int)(k%100UL);
  t.tm_hour=12;
  return mktime(&t);
}

uint32_t historyRecordOffset(uint32_t index) {
  return sizeof(HistoryHeader)+index*sizeof(DailyHistoryRecord);
}


bool historyReadRecordFromOpenFile(File& f, uint32_t physicalIndex, DailyHistoryRecord& r) {
  if (!f || physicalIndex >= historyHeader.capacity) return false;
  if (!f.seek(historyRecordOffset(physicalIndex), SeekSet)) return false;
  if (f.read(reinterpret_cast<uint8_t*>(&r), sizeof(r)) != sizeof(r)) return false;
  if (!r.dayKey) return false;
  return r.crc16 == historyRecordCrc(r);
}

bool historyReadChronologicalFromOpenFile(File& f, uint32_t chronologicalIndex, DailyHistoryRecord& r) {
  if (!historyReady || chronologicalIndex >= historyHeader.count) return false;
  const uint32_t oldest = historyOldestPhysicalIndex();
  const uint32_t physical = (oldest + chronologicalIndex) % historyHeader.capacity;
  return historyReadRecordFromOpenFile(f, physical, r);
}

bool historyWriteRecordToOpenFile(File& f,uint32_t physicalIndex,DailyHistoryRecord& r) {
  if(!f || physicalIndex>=historyHeader.capacity)return false;

  r.crc16=historyRecordCrc(r);

  if(!f.seek(historyRecordOffset(physicalIndex),SeekSet))return false;

  return f.write(reinterpret_cast<const uint8_t*>(&r),sizeof(r))==sizeof(r);
}

void historyInvalidateStatsCache() {
  historyStatsCache.valid = false;
}


bool historyWriteHeader(File& f) {
  historyHeader.crc=historyHeaderCrc(historyHeader);
  if(!f.seek(0,SeekSet))return false;
  return f.write(reinterpret_cast<const uint8_t*>(&historyHeader),sizeof(historyHeader))==sizeof(historyHeader);
}

bool historyReadRecordByPhysicalIndex(uint32_t idx, DailyHistoryRecord& r) {
  if(!historyReady||idx>=historyHeader.capacity)return false;
  File f=LittleFS.open(HISTORY_FILE,"r");
  if(!f)return false;
  bool ok=f.seek(historyRecordOffset(idx),SeekSet) &&
          f.read(reinterpret_cast<uint8_t*>(&r),sizeof(r))==sizeof(r);
  f.close();
  return ok && r.dayKey && r.crc16==historyRecordCrc(r);
}

bool historyWriteRecordAt(uint32_t idx, DailyHistoryRecord& r, bool updateHeader) {
  if(!historyReady||idx>=historyHeader.capacity)return false;
