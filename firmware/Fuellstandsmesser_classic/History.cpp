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
  r.crc16=historyRecordCrc(r);
  File f=LittleFS.open(HISTORY_FILE,"r+");
  if(!f){historyWriteErrors++;return false;}
  bool ok=f.seek(historyRecordOffset(idx),SeekSet) &&
          f.write(reinterpret_cast<const uint8_t*>(&r),sizeof(r))==sizeof(r);
  if(ok&&updateHeader)ok=historyWriteHeader(f);
  f.flush();f.close();
  if(ok){
    historyWriteCount++;
    historyInvalidateStatsCache();
  }else{
    historyWriteErrors++;
  }
  return ok;
}

uint32_t historyOldestPhysicalIndex() {
  if(historyHeader.count==0||historyHeader.count<historyHeader.capacity)return 0;
  return historyHeader.writeIndex;
}

bool historyReadChronological(uint32_t i, DailyHistoryRecord& r) {
  if(!historyReady||i>=historyHeader.count)return false;
  uint32_t p=(historyOldestPhysicalIndex()+i)%historyHeader.capacity;
  return historyReadRecordByPhysicalIndex(p,r);
}

int32_t historyFindDay(uint32_t dayKey, DailyHistoryRecord* out=nullptr) {
  if (!historyReady || !dayKey) return -1;

  File f = LittleFS.open(HISTORY_FILE, "r");
  if (!f) return -1;

  const uint32_t oldest = historyOldestPhysicalIndex();

  for (uint32_t i=0; i<historyHeader.count; ++i) {
    const uint32_t physical = (oldest + i) % historyHeader.capacity;
    DailyHistoryRecord r;
    if (!historyReadRecordFromOpenFile(f, physical, r)) continue;

    if (r.dayKey == dayKey) {
      if (out) *out = r;
      f.close();
      return (int32_t)physical;
    }

    if ((i & 0x7F) == 0) yield();
  }

  f.close();
  return -1;
}


bool historyDaysAreAdjacent(uint32_t olderDayKey, uint32_t newerDayKey) {
  if(!olderDayKey || !newerDayKey || olderDayKey>=newerDayKey) return false;
  const time_t a=historyDayKeyToTime(olderDayKey);
  const time_t b=historyDayKeyToTime(newerDayKey);
  if(a<=0 || b<=0) return false;
  const long diff=(long)(b-a);
  return diff >= 20L*3600L && diff <= 28L*3600L;
}

bool historyFindPreviousDay(uint32_t dayKey, DailyHistoryRecord& prev) {
  if(!historyReady || historyHeader.count==0) return false;

  File f=LittleFS.open(HISTORY_FILE,"r");
  if(!f) return false;

  const uint32_t oldest=historyOldestPhysicalIndex();
  bool found=false;
  DailyHistoryRecord best={};

  for(uint32_t i=0;i<historyHeader.count;i++){
    const uint32_t physical=(oldest+i)%historyHeader.capacity;
    DailyHistoryRecord r;
    if(!historyReadRecordFromOpenFile(f,physical,r))continue;
    if(r.dayKey>=dayKey)continue;
    if(r.source==HISTORY_TEST)continue;

    if(!found || r.dayKey>best.dayKey){
      best=r;
      found=true;
    }

    if((i&0x7F)==0)yield();
  }

  f.close();

  if(!found || !historyDaysAreAdjacent(best.dayKey,dayKey)) return false;
  prev=best;
  return true;
}

void historyPrepareDayBaseline(uint32_t dayKey) {
  historyDayBaselineValid=false;
  historyDayBaselineLiters=0;
  historyDayRefillConfirmed=false;
  historyDayMaxRiseLiters=0;

  DailyHistoryRecord prev;
  if(historyFindPreviousDay(dayKey,prev)){
    historyDayBaselineLiters=prev.levelLiters;
    historyDayBaselineValid=true;

    Serial.print(F("[HISTORY] Tagesstart aus Vortag "));
    Serial.print(historyDateString(prev.dayKey));
    Serial.print(F(" = "));
    Serial.print(historyDayBaselineLiters);
    Serial.println(F(" L"));
  }else{
    Serial.println(F("[HISTORY] Kein lueckenloser Vortag -> Tagesstart ab erster Messung"));
  }
}

void historyResetCurrentFromRecord(const DailyHistoryRecord& r,uint32_t physicalIndex){
  historyCurrent=r;
  historyCurrentIndex=physicalIndex;
  historyCurrentValid=true;
  historyPercentSum=(uint64_t)r.avgPermille*(uint64_t)max((uint16_t)1,r.samples);

  historyDayBaselineLiters=r.firstLiters;
  historyDayBaselineValid=(r.flags&HISTORY_FLAG_PREV_DAY_BASELINE)!=0;
  historyDayRefillConfirmed=r.refillLiters>=HISTORY_REFILL_MIN_LITERS;
  historyDayMaxRiseLiters=r.refillLiters;
}

void historyStartNewDay(uint32_t dayKey,uint8_t source=HISTORY_MEASURED){
  DailyHistoryRecord existingRecord;
  int32_t existingIndex=historyFindDay(dayKey,&existingRecord);

  if(existingIndex>=0){
    historyResetCurrentFromRecord(existingRecord,(uint32_t)existingIndex);

    Serial.print(F("[HISTORY] Tag bereits vorhanden "));
    Serial.print(historyDateString(dayKey));
    Serial.print(F(" -> fortgesetzt index="));
    Serial.print(existingIndex);
    Serial.print(F(" source="));
    Serial.println(existingRecord.source);

    return;
  }

  historyPrepareDayBaseline(dayKey);
  memset(&historyCurrent,0,sizeof(historyCurrent));
  historyClimateClear(historyCurrent);
  historyCurrent.dayKey=dayKey;
  historyCurrent.minPermille=1000;
  historyCurrent.source=source;
  if(historyDayBaselineValid){
    historyCurrent.firstLiters=historyDayBaselineLiters;
    historyCurrent.flags|=HISTORY_FLAG_PREV_DAY_BASELINE;
  }
  historyCurrentValid=true;
  historyPercentSum=0;
  historyCurrentIndex=historyHeader.writeIndex;

  if(historyHeader.count<historyHeader.capacity)historyHeader.count++;
  historyHeader.writeIndex=(historyHeader.writeIndex+1)%historyHeader.capacity;

  Serial.print(F("[HISTORY] Neuer Tag "));
  Serial.print(historyDateString(dayKey));
  Serial.print(F(" index="));
  Serial.println(historyCurrentIndex);
}

void historyRecalcConsumption(DailyHistoryRecord& r) {
  if(r.source!=HISTORY_MEASURED)return;
  if(r.firstLiters==0 && r.levelLiters==0)return;

  const int32_t start=(int32_t)r.firstLiters;
  const int32_t end=(int32_t)r.levelLiters;
  const int32_t rise=end-start;

  if(rise >= (int32_t)HISTORY_REFILL_MIN_LITERS){
    const uint16_t candidate=(uint16_t)min((int32_t)65535,rise);
    if(candidate>historyDayMaxRiseLiters)historyDayMaxRiseLiters=candidate;

    if(!historyDayRefillConfirmed){
      historyDayRefillConfirmed=true;
      Serial.print(F("[HISTORY] Nachfuellung bestaetigt +"));
      Serial.print(historyDayMaxRiseLiters);
      Serial.println(F(" L"));
    }
  }

  if(historyDayRefillConfirmed){
    r.refillLiters=max(r.refillLiters,historyDayMaxRiseLiters);
    const int32_t cons=start+(int32_t)r.refillLiters-end;
    r.consumptionLiters=(uint16_t)constrain((int)max((int32_t)0,cons),0,65535);
  }else{
    r.refillLiters=0;
    const int32_t cons=start-end;
    r.consumptionLiters=(uint16_t)constrain((int)max((int32_t)0,cons),0,65535);
  }
}

void historyPromoteCurrentTestDayToMeasured(){
  if(!historyCurrentValid || historyCurrent.source!=HISTORY_TEST)return;

  const uint32_t dayKey=historyCurrent.dayKey;
  const uint32_t physicalIndex=historyCurrentIndex;

  Serial.print(F("[HISTORY] Testtag -> reale Messung, Record wird neu aufgebaut: "));
  Serial.println(historyDateString(dayKey));

  historyPrepareDayBaseline(dayKey);

  memset(&historyCurrent,0,sizeof(historyCurrent));
  historyClimateClear(historyCurrent);
  historyCurrent.dayKey=dayKey;
  historyCurrent.minPermille=1000;
  historyCurrent.source=HISTORY_MEASURED;
  historyCurrentIndex=physicalIndex;
  historyCurrentValid=true;
  historyPercentSum=0;

  if(historyDayBaselineValid){
    historyCurrent.firstLiters=historyDayBaselineLiters;
    historyCurrent.flags|=HISTORY_FLAG_PREV_DAY_BASELINE;
  }
}

void historyAccumulateCurrent(){
  if(!historyCurrentValid||!isfinite(tankPercent)||!isfinite(tankLiters))return;
  uint16_t p=(uint16_t)constrain((int)lroundf(tankPercent*10.0f),0,1000);
  uint16_t l=(uint16_t)constrain((int)lroundf(tankLiters),0,65535);
  if(historyCurrent.samples==0){
    if(!historyDayBaselineValid){
      historyCurrent.firstLiters=l;
      historyDayBaselineLiters=l;
    }
    historyCurrent.minPermille=p;
    historyCurrent.maxPermille=p;
  }
  if(historyCurrent.samples<65535)historyCurrent.samples++;
  historyPercentSum+=p;
  historyCurrent.avgPermille=(uint16_t)(historyPercentSum/historyCurrent.samples);
  historyCurrent.minPermille=min(historyCurrent.minPermille,p);
  historyCurrent.maxPermille=max(historyCurrent.maxPermille,p);
  historyCurrent.levelLiters=l;
  historyCurrent.source=HISTORY_MEASURED;
  historyClimateAccumulate(historyCurrent);
  historyRecalcConsumption(historyCurrent);
}

void historyCheckpoint(bool force){
  if(!historyReady||!historyCurrentValid||!historyCurrent.samples)return;
  uint32_t nowMs=millis();
  if(!force&&(uint32_t)(nowMs-historyLastCheckpointMs)<HISTORY_CHECKPOINT_MS)return;
  bool ok=historyWriteRecordAt(historyCurrentIndex,historyCurrent,true);
  historyLastCheckpointMs=nowMs;
  Serial.print(F("[HISTORY] Checkpoint "));Serial.print(historyDateString(historyCurrent.dayKey));
  Serial.print(F(" samples="));Serial.print(historyCurrent.samples);
  Serial.println(ok?F(" OK"):F(" FEHLER"));
}

void historyOnMeasurement(){
  if(!historyReady||!isfinite(tankPercent)||!isfinite(tankLiters))return;
  uint32_t dayKey=0;
  if(!historyDateNow(dayKey)){historyTimeValid=false;return;}
  historyTimeValid=true;
  if(!historyCurrentValid)historyStartNewDay(dayKey);
  else if(historyCurrent.dayKey!=dayKey){historyCheckpoint(true);historyStartNewDay(dayKey);}

  if(historyCurrentValid && historyCurrent.source==HISTORY_TEST){
    historyPromoteCurrentTestDayToMeasured();
  }

  historyAccumulateCurrent();
  historyCheckpoint(historyCurrent.samples==1);
}


bool historyRepairIndexCreate(File& idxFile,uint32_t slots){
  if(!idxFile || slots==0)return false;
  const uint32_t empty=0xFFFFFFFFUL;
  for(uint32_t i=0;i<slots;i++){
    if(idxFile.write(reinterpret_cast<const uint8_t*>(&empty),sizeof(empty))!=sizeof(empty))return false;
    if((i&0x7F)==0)yield();
  }
  idxFile.flush();
  return true;
}

bool historyRepairIndexWrite(File& idxFile,uint32_t slot,uint32_t physicalIndex){
  if(!idxFile)return false;
  const uint32_t offset=slot*sizeof(uint32_t);
  if(!idxFile.seek(offset,SeekSet))return false;
  return idxFile.write(reinterpret_cast<const uint8_t*>(&physicalIndex),sizeof(physicalIndex))==sizeof(physicalIndex);
}

bool historyRepairIndexRead(File& idxFile,uint32_t slot,uint32_t& physicalIndex){
  physicalIndex=0xFFFFFFFFUL;
  if(!idxFile)return false;
  const uint32_t offset=slot*sizeof(uint32_t);
  if(offset+sizeof(uint32_t)>idxFile.size())return true;
  if(!idxFile.seek(offset,SeekSet))return false;
  return idxFile.read(reinterpret_cast<uint8_t*>(&physicalIndex),sizeof(physicalIndex))==sizeof(physicalIndex);
}

// Fast-Import verwendet dasselbe kompakte Dateindex-Format wie der

// Explicit C++ forward declarations replacing Arduino auto-prototypes.
bool historyDateNow(uint32_t& dayKey);
bool historyNewestRecordFromOpenFile(File& f, DailyHistoryRecord& r, uint32_t& logicalIndex);
uint32_t historyFirstDayForAnchor(uint32_t anchorDay, uint16_t days);
uint32_t historyOldestPhysicalIndex();
bool historyReadRecordFromOpenFile(File& f, uint32_t physicalIndex, DailyHistoryRecord& r);
void setupFilesystem();

bool historyImportIndexCreate(File& idxFile,uint32_t slots){
  return historyRepairIndexCreate(idxFile,slots);
}

bool historyImportIndexWrite(File& idxFile,uint32_t slot,uint32_t physicalIndex){
  return historyRepairIndexWrite(idxFile,slot,physicalIndex);
}

bool historyImportIndexRead(File& idxFile,uint32_t slot,uint32_t& physicalIndex){
  return historyRepairIndexRead(idxFile,slot,physicalIndex);
}

bool historyIntegrityCheckAndRepair(){
  historyRepairDuplicates=0;
  historyRepairInvalid=0;
  historyRepairOutOfOrder=0;
  historyRepairRemoved=0;
  historyRepairPerformed=false;

  if(!historyReady || historyHeader.count==0)return true;

  Serial.print(F("[HISTORY REPAIR] Start count="));
  Serial.println(historyHeader.count);
  yield();

  File src=LittleFS.open(HISTORY_FILE,"r");
  if(!src){
    Serial.println(F("[HISTORY REPAIR] History-Datei nicht lesbar"));
    return false;
  }

  const uint32_t oldestPhysical=historyOldestPhysicalIndex();
  Serial.print(F("[HISTORY REPAIR] Phase 1 scan oldest="));
  Serial.println(oldestPhysical);
  yield();

  int32_t minOrd=INT32_MAX;
  int32_t maxOrd=INT32_MIN;
  int32_t prevOrd=INT32_MIN;
  uint32_t validRecords=0;

  for(uint32_t li=0;li<historyHeader.count;li++){
    const uint32_t physical=(oldestPhysical+li)%historyHeader.capacity;
    DailyHistoryRecord r;

    if(!historyReadRecordFromOpenFile(src,physical,r)){
      historyRepairInvalid++;
      if((li&0x7F)==0)yield();
      continue;
    }

    const int32_t ord=historyDayOrdinal(r.dayKey);
    if(ord<0){
      historyRepairInvalid++;
      if((li&0x7F)==0)yield();
      continue;
    }

    if(prevOrd!=INT32_MIN && ord<prevOrd)historyRepairOutOfOrder++;
    prevOrd=ord;

    if(ord<minOrd)minOrd=ord;
    if(ord>maxOrd)maxOrd=ord;
    validRecords++;
    if((li&0x1F)==0)yield();
  }

  Serial.print(F("[HISTORY REPAIR] Phase 1 fertig valid="));
  Serial.print(validRecords);
  Serial.print(F(" invalid="));
  Serial.print(historyRepairInvalid);
  Serial.print(F(" minOrd="));
  Serial.print(minOrd);
  Serial.print(F(" maxOrd="));
  Serial.println(maxOrd);
  yield();

  if(validRecords==0 || minOrd>maxOrd){
    src.close();
    Serial.println(F("[HISTORY REPAIR] Keine gueltigen Records gefunden"));
    return false;
  }

  const uint32_t slots=(uint32_t)(maxOrd-minOrd+1);
  Serial.print(F("[HISTORY REPAIR] Index slots="));
  Serial.println(slots);
  yield();

  if(slots>50000UL){
    src.close();
    Serial.print(F("[HISTORY REPAIR] Datumsbereich unplausibel slots="));
    Serial.println(slots);
    return false;
  }

  LittleFS.remove(HISTORY_REPAIR_INDEX_FILE);
  File idx=LittleFS.open(HISTORY_REPAIR_INDEX_FILE,"w+");
  if(!idx){
    src.close();
    Serial.println(F("[HISTORY REPAIR] Index-Datei konnte nicht erstellt werden"));
    return false;
  }

  Serial.println(F("[HISTORY REPAIR] Phase 2 Index initialisieren"));
  yield();

  if(!historyRepairIndexCreate(idx,slots)){
    src.close();idx.close();
    LittleFS.remove(HISTORY_REPAIR_INDEX_FILE);
    Serial.println(F("[HISTORY REPAIR] Index Initialisierung fehlgeschlagen"));
    return false;
  }

  Serial.println(F("[HISTORY REPAIR] Phase 3 Tagesindex aufbauen"));
  yield();

  for(uint32_t li=0;li<historyHeader.count;li++){
    const uint32_t physical=(oldestPhysical+li)%historyHeader.capacity;
    DailyHistoryRecord r;
    if(!historyReadRecordFromOpenFile(src,physical,r))continue;

    const int32_t ord=historyDayOrdinal(r.dayKey);
    if(ord<minOrd||ord>maxOrd)continue;

    const uint32_t slot=(uint32_t)(ord-minOrd);
    uint32_t previous=0xFFFFFFFFUL;

    if(!historyRepairIndexRead(idx,slot,previous)){
      src.close();idx.close();
      LittleFS.remove(HISTORY_REPAIR_INDEX_FILE);
      Serial.println(F("[HISTORY REPAIR] Index lesen fehlgeschlagen"));
      return false;
    }

    if(previous!=0xFFFFFFFFUL)historyRepairDuplicates++;

    if(!historyRepairIndexWrite(idx,slot,physical)){
      src.close();idx.close();
      LittleFS.remove(HISTORY_REPAIR_INDEX_FILE);
      Serial.println(F("[HISTORY REPAIR] Index schreiben fehlgeschlagen"));
      return false;
    }

    if((li&0x1F)==0)yield();
  }
  idx.flush();
  yield();

  Serial.print(F("[HISTORY REPAIR] Phase 3 fertig duplicates="));
  Serial.println(historyRepairDuplicates);

  const bool needsRepair=
    historyRepairDuplicates>0 ||
    historyRepairInvalid>0 ||
    historyRepairOutOfOrder>0;

  if(!needsRepair){
    src.close();idx.close();
    LittleFS.remove(HISTORY_REPAIR_INDEX_FILE);

    Serial.print(F("[HISTORY REPAIR] OK records="));
    Serial.print(historyHeader.count);
    Serial.println(F(" duplicates=0 invalid=0 order=OK"));
    return true;
  }

  Serial.print(F("[HISTORY REPAIR] Reparatur notwendig duplicates="));
  Serial.print(historyRepairDuplicates);
  Serial.print(F(" invalid="));
  Serial.print(historyRepairInvalid);
  Serial.print(F(" outOfOrder="));
  Serial.println(historyRepairOutOfOrder);

  Serial.println(F("[HISTORY REPAIR] Phase 4 kompakte Datei schreiben"));
  yield();

  LittleFS.remove(HISTORY_REPAIR_TMP_FILE);
  File tmp=LittleFS.open(HISTORY_REPAIR_TMP_FILE,"w+");
  if(!tmp){
    src.close();idx.close();
    LittleFS.remove(HISTORY_REPAIR_INDEX_FILE);
    Serial.println(F("[HISTORY REPAIR] Temp-Datei konnte nicht erstellt werden"));
    return false;
  }

  HistoryHeader newHeader=historyHeader;
  newHeader.count=0;
  newHeader.writeIndex=0;
  newHeader.crc=historyHeaderCrc(newHeader);

  if(tmp.write(reinterpret_cast<const uint8_t*>(&newHeader),sizeof(newHeader))!=sizeof(newHeader)){
    src.close();idx.close();tmp.close();
    LittleFS.remove(HISTORY_REPAIR_INDEX_FILE);
    LittleFS.remove(HISTORY_REPAIR_TMP_FILE);
    Serial.println(F("[HISTORY REPAIR] Temp-Header schreiben fehlgeschlagen"));
    return false;
  }

  uint32_t newCount=0;

  for(uint32_t slot=0;slot<slots;slot++){
    uint32_t physical=0xFFFFFFFFUL;
    if(!historyRepairIndexRead(idx,slot,physical))continue;
    if(physical==0xFFFFFFFFUL)continue;

    DailyHistoryRecord r;
    if(!historyReadRecordFromOpenFile(src,physical,r))continue;

    const uint32_t offset=sizeof(HistoryHeader)+newCount*sizeof(DailyHistoryRecord);
    if(!tmp.seek(offset,SeekSet))continue;

    if(tmp.write(reinterpret_cast<const uint8_t*>(&r),sizeof(r))!=sizeof(r))continue;

    newCount++;
    if((newCount&0x1F)==0)yield();
  }

  newHeader.count=newCount;
  newHeader.writeIndex=newCount%newHeader.capacity;
  newHeader.crc=historyHeaderCrc(newHeader);

  if(!tmp.seek(0,SeekSet) ||
     tmp.write(reinterpret_cast<const uint8_t*>(&newHeader),sizeof(newHeader))!=sizeof(newHeader)){
    src.close();idx.close();tmp.close();
    LittleFS.remove(HISTORY_REPAIR_INDEX_FILE);
    LittleFS.remove(HISTORY_REPAIR_TMP_FILE);
    Serial.println(F("[HISTORY REPAIR] finaler Header fehlgeschlagen"));
    return false;
  }

  tmp.flush();
  src.close();
  idx.close();
  tmp.close();

  Serial.println(F("[HISTORY REPAIR] Phase 5 Dateien tauschen"));
  yield();

  LittleFS.remove(HISTORY_REPAIR_BAK_FILE);

  if(!LittleFS.rename(HISTORY_FILE,HISTORY_REPAIR_BAK_FILE)){
    LittleFS.remove(HISTORY_REPAIR_INDEX_FILE);
    LittleFS.remove(HISTORY_REPAIR_TMP_FILE);
    Serial.println(F("[HISTORY REPAIR] Backup-Rename fehlgeschlagen"));
    return false;
  }

  if(!LittleFS.rename(HISTORY_REPAIR_TMP_FILE,HISTORY_FILE)){
    LittleFS.rename(HISTORY_REPAIR_BAK_FILE,HISTORY_FILE);
    LittleFS.remove(HISTORY_REPAIR_INDEX_FILE);
    LittleFS.remove(HISTORY_REPAIR_TMP_FILE);
    Serial.println(F("[HISTORY REPAIR] Neue History konnte nicht aktiviert werden"));
    return false;
  }

  LittleFS.remove(HISTORY_REPAIR_BAK_FILE);
  LittleFS.remove(HISTORY_REPAIR_INDEX_FILE);

  historyRepairRemoved=
    historyHeader.count>newCount ? historyHeader.count-newCount : 0;

  historyHeader=newHeader;
  historyCurrentValid=false;
  historyInvalidateStatsCache();
  historyRepairPerformed=true;

  Serial.print(F("[HISTORY REPAIR] Fertig alt="));
  Serial.print(validRecords + historyRepairInvalid);
  Serial.print(F(" neu="));
  Serial.print(newCount);
  Serial.print(F(" entfernt="));
  Serial.println(historyRepairRemoved);

  return true;
}

void historySetupAfterFilesystem(){
  historyReady=false;historyCurrentValid=false;
  if(!fsMounted||!LittleFS.info(fsInfoCache)){
    Serial.println(F("[HISTORY] deaktiviert: LittleFS nicht verfuegbar"));
    return;
  }

  uint32_t usable=fsInfoCache.totalBytes>HISTORY_RESERVE_BYTES
    ?fsInfoCache.totalBytes-HISTORY_RESERVE_BYTES:0;
  uint32_t capacity=usable>sizeof(HistoryHeader)
    ?(usable-sizeof(HistoryHeader))/sizeof(DailyHistoryRecord):0;

  if(capacity<365){
    Serial.println(F("[HISTORY] deaktiviert: zu wenig LittleFS"));
    return;
  }

  if(LittleFS.exists(HISTORY_FILE)){
    File probe=LittleFS.open(HISTORY_FILE,"r");
    if(probe){
      HistoryHeader h{};
      if(probe.read(reinterpret_cast<uint8_t*>(&h),sizeof(h))==sizeof(h) &&
         h.magic==HISTORY_MAGIC &&
         h.version==HISTORY_VERSION_V2 &&
         h.recordSize==sizeof(DailyHistoryRecordV2)){
        probe.close();
        if(!historyMigrateV2ToV3(capacity)){
          Serial.println(F("[HISTORY V3] Migration FEHLER - alte History bleibt erhalten"));
          return;
        }
      }else{
        probe.close();
      }
    }
  }

  memset(&historyHeader,0,sizeof(historyHeader));
  bool valid=false;

  if(LittleFS.exists(HISTORY_FILE)){
    File f=LittleFS.open(HISTORY_FILE,"r");
    if(f){
      if(f.read(reinterpret_cast<uint8_t*>(&historyHeader),sizeof(historyHeader))==sizeof(historyHeader)){
        valid=historyHeader.magic==HISTORY_MAGIC&&
              historyHeader.version==HISTORY_VERSION&&
              historyHeader.recordSize==sizeof(DailyHistoryRecord)&&
              historyHeader.capacity>0&&
              historyHeader.capacity<=capacity&&
              historyHeader.count<=historyHeader.capacity&&
              historyHeader.writeIndex<historyHeader.capacity&&
              historyHeader.crc==historyHeaderCrc(historyHeader);
      }
      f.close();
    }
  }

  if(!valid){
    LittleFS.remove(HISTORY_FILE);
    File f=LittleFS.open(HISTORY_FILE,"w+");
    if(!f){Serial.println(F("[HISTORY] Datei anlegen FEHLER"));return;}
    historyHeader.magic=HISTORY_MAGIC;
    historyHeader.version=HISTORY_VERSION;
    historyHeader.recordSize=sizeof(DailyHistoryRecord);
    historyHeader.capacity=capacity;
    historyHeader.count=0;
    historyHeader.writeIndex=0;
    historyHeader.crc=historyHeaderCrc(historyHeader);
    bool ok=f.write(reinterpret_cast<const uint8_t*>(&historyHeader),sizeof(historyHeader))==sizeof(historyHeader);
    f.flush();f.close();
    if(!ok)return;
    Serial.println(F("[HISTORY] neue History V3 mit Klima angelegt"));
  }else{
    Serial.println(F("[HISTORY] vorhandene History V3 geladen"));
  }

  historyReady=true;
  Serial.println(F("[HISTORY] Fast-Read aktiviert: Datei bleibt pro Auswertung offen"));
  Serial.println(F("[HISTORY] Duplicate-Day Guard aktiv"));
  Serial.print(F("[HISTORY] version="));Serial.print(HISTORY_VERSION);
  Serial.print(F(" record="));Serial.print(sizeof(DailyHistoryRecord));
  Serial.print(F(" B capacity="));Serial.print(historyHeader.capacity);
  Serial.print(F(" Tage (~"));Serial.print((float)historyHeader.capacity/365.25f,1);
  Serial.print(F(" Jahre) count="));Serial.println(historyHeader.count);
  Serial.println(F("[HISTORY] Klima: T avg/min/max + RH avg/min/max"));
  Serial.println(F("[HISTORY] Integritaetscheck: manuell ueber Wartung"));
}

void historySetupTime(){
  if(WiFi.status()!=WL_CONNECTED){Serial.println(F("[TIME] NTP wartet auf WLAN"));return;}
  setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1);tzset();
  configTime(0,0,"pool.ntp.org","time.nist.gov");
  Serial.println(F("[TIME] NTP gestartet"));
  uint32_t start=millis();
  while(time(nullptr)<1700000000&&millis()-start<5000UL){delay(100);yield();}
  uint32_t dayKey=0;
  if(historyDateNow(dayKey)){
    historyTimeValid=true;Serial.print(F("[TIME] Datum "));Serial.println(historyDateString(dayKey));
    DailyHistoryRecord r;
    int32_t p=historyFindDay(dayKey,&r);
    if(p>=0){
      historyResetCurrentFromRecord(r,(uint32_t)p);
      Serial.print(F("[HISTORY] heutigen Datensatz fortgesetzt index="));
      Serial.print(p);
      Serial.print(F(" source="));
      Serial.println(r.source);
    }
  }
}

void historyLoop(){
  if(!historyReady)return;
  if(!historyTimeValid&&WiFi.status()==WL_CONNECTED){
    static uint32_t retry=0;
    if(millis()-retry>=60000UL){retry=millis();uint32_t d=0;if(historyDateNow(d))historyTimeValid=true;}
  }
}

uint32_t historyFirstDayForAnchor(uint32_t anchorDay,uint16_t days){
  if(anchorDay==0 || days==0)return 0;
  time_t t=historyDayKeyToTime(anchorDay);
  if(t==(time_t)-1 || t<=0)return 0;
  return historyDayKeyFromTime(t-(time_t)(days-1)*86400);
}

bool historyNewestRecordFromOpenFile(File& f,DailyHistoryRecord& out,uint32_t& logicalIndex){
  if(historyHeader.count==0)return false;
  const uint32_t oldest=historyOldestPhysicalIndex();

  for(uint32_t back=0;back<historyHeader.count;back++){
    const uint32_t li=historyHeader.count-1-back;
    const uint32_t physical=(oldest+li)%historyHeader.capacity;
    DailyHistoryRecord r;
    if(historyReadRecordFromOpenFile(f,physical,r) && r.dayKey>0){
      out=r;
      logicalIndex=li;
      return true;
    }
    if((back&0x3F)==0)yield();
  }
  return false;
}


uint32_t historyFirstDayForDays(uint16_t days){
  if(days==0)return 0;

  uint32_t anchorDay=0;
  if(historyDateNow(anchorDay)){
    return historyFirstDayForAnchor(anchorDay,days);
  }

  if(!historyReady || historyHeader.count==0)return 0;

  File f=LittleFS.open(HISTORY_FILE,"r");
  if(!f)return 0;

  DailyHistoryRecord newest;
  uint32_t newestLogical=0;
  const bool ok=historyNewestRecordFromOpenFile(f,newest,newestLogical);
  f.close();

  if(!ok || newest.dayKey==0)return 0;

  return historyFirstDayForAnchor(newest.dayKey,days);
}

bool historyChronologicalBoundsFromOpenFile(
  File& f,
  DailyHistoryRecord& oldestRecord,
  uint32_t& oldestLogical,
  DailyHistoryRecord& newestRecord,
  uint32_t& newestLogical
){
  if(historyHeader.count==0)return false;

  bool have=false;
  uint32_t minDay=0xFFFFFFFFUL;
  uint32_t maxDay=0;

  for(uint32_t li=0;li<historyHeader.count;li++){
    DailyHistoryRecord r;
    if(!historyReadChronologicalFromOpenFile(f,li,r)){
      if((li&0x7F)==0)yield();
      continue;
    }
    if(r.dayKey==0)continue;

    if(!have || r.dayKey<minDay){
      minDay=r.dayKey;
      oldestRecord=r;
      oldestLogical=li;
    }
    if(!have || r.dayKey>=maxDay){
      maxDay=r.dayKey;
      newestRecord=r;
      newestLogical=li;
    }
    have=true;
    if((li&0x7F)==0)yield();
  }

  return have;
}

float historyCapacityLiters(){return tankCapacityLiters();}

float historyPercentForLiters(float liters){
  float cap=historyCapacityLiters();
  return cap>0?constrain(liters/cap*100.0f,0.0f,100.0f):0.0f;
}

void handleHistoryApi(){
  historyApiRequests++;
  const uint32_t apiStartMs=millis();

  uint16_t days=365;
  if(server.hasArg("days")){
    long d=server.arg("days").toInt();
    if(d>0&&d<=3650)days=(uint16_t)d;
  }

  if(!historyReady){
    historyApiErrors++;
    server.send(503,"application/json",
      "{\"ok\":false,\"error\":\"history_not_ready\",\"items\":[],\"stats\":{\"days\":0}}");
    return;
  }

  File f=LittleFS.open(HISTORY_FILE,"r");
  if(!f){
    historyApiErrors++;
    server.send(500,"application/json",
      "{\"ok\":false,\"error\":\"history_file_open_failed\",\"items\":[],\"stats\":{\"days\":0}}");
    return;
  }

  DailyHistoryRecord oldestRecord{},newestRecord{};
  uint32_t oldestLogical=0,newestLogical=0;
  const bool haveBounds=historyChronologicalBoundsFromOpenFile(
    f,oldestRecord,oldestLogical,newestRecord,newestLogical);

  uint32_t anchorDay=0;
  bool anchorFromClock=historyDateNow(anchorDay);
  if(!anchorFromClock){
    if(haveBounds)anchorDay=newestRecord.dayKey;
  }else if(haveBounds && newestRecord.dayKey>anchorDay){
    anchorDay=newestRecord.dayKey;
    anchorFromClock=false;
  }

  const uint32_t first=historyFirstDayForAnchor(anchorDay,days);

  auto logicalAtChronologicalOffset=[&](uint32_t offset)->uint32_t{
    if(historyHeader.count==0)return 0;
    return (oldestLogical+offset)%historyHeader.count;
  };

  uint32_t scanned=0,uniqueEligible=0,duplicatesSkipped=0;
  DailyHistoryRecord pending{};
  bool havePending=false;

  auto countPending=[&]()->void{
    if(!havePending)return;
    if((!first || pending.dayKey>=first) && (!anchorDay || pending.dayKey<=anchorDay)){
      uniqueEligible++;
    }
  };

  for(uint32_t off=0;off<historyHeader.count;off++){
    const uint32_t li=logicalAtChronologicalOffset(off);
    DailyHistoryRecord r;
    scanned++;
    if(!historyReadChronologicalFromOpenFile(f,li,r)){
      if((scanned&0x7F)==0)yield();
      continue;
    }

    if(!havePending){
      pending=r;havePending=true;
    }else if(r.dayKey==pending.dayKey){
      pending=r;
      duplicatesSkipped++;
    }else{
      countPending();
      pending=r;
    }
    if((scanned&0x7F)==0)yield();
  }
  countPending();

  const uint32_t maxPoints=365;
  const uint32_t calculatedStep=(uniqueEligible+maxPoints-1U)/maxPoints;
  const uint32_t step=(calculatedStep<1U)?1U:calculatedStep;

  webPrepareConnectionClose();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200,"application/json; charset=utf-8","");

  server.sendContent(F("{\"ok\":true,\"items\":["));

  bool comma=false;
  uint32_t eligible=0,emitted=0,ordinalEligible=0;
  float totalC=0,totalR=0,minP=101,maxP=-1;

  auto emitPending=[&]()->void{
    if(!havePending)return;
    if(first && pending.dayKey<first)return;
    if(anchorDay && pending.dayKey>anchorDay)return;

    eligible++;
    totalC+=(float)pending.consumptionLiters;
    totalR+=(float)pending.refillLiters;
    const float pct=historyPercentForLiters(pending.levelLiters);
    if(pct<minP)minP=pct;
    if(pct>maxP)maxP=pct;

    const bool emit=(ordinalEligible%step)==0 || pending.dayKey==anchorDay;
    ordinalEligible++;
    if(!emit)return;

    const time_t ts=historyDayKeyToTime(pending.dayKey);
    if(ts==(time_t)-1 || ts<=0)return;

    char tbuf[24];
    snprintf(tbuf,sizeof(tbuf),"%llu",
             (unsigned long long)((uint64_t)(uint32_t)ts*1000ULL));

    if(comma)server.sendContent(F(","));
    comma=true;

    char item[176];
    const int n=snprintf(
      item,sizeof(item),
      "{\"time\":%s,\"percent\":%.1f,\"liters\":%u,"
      "\"consumedLiters\":%u,\"refillLiters\":%u,\"source\":%u}",
      tbuf,(double)pct,
      (unsigned)pending.levelLiters,
      (unsigned)pending.consumptionLiters,
      (unsigned)pending.refillLiters,
      (unsigned)pending.source
    );

    if(n>0 && (size_t)n<sizeof(item))server.sendContent(item);
    emitted++;
    yield();
  };

  havePending=false;
  for(uint32_t off=0;off<historyHeader.count;off++){
    const uint32_t li=logicalAtChronologicalOffset(off);
    DailyHistoryRecord r;
    if(!historyReadChronologicalFromOpenFile(f,li,r)){
      if((off&0x7F)==0)yield();
      continue;
    }

    if(!havePending){
      pending=r;havePending=true;
    }else if(r.dayKey==pending.dayKey){
      pending=r;
    }else{
      emitPending();
      pending=r;
    }
    if((off&0x7F)==0)yield();
  }
  emitPending();

  f.close();

  server.sendContent(F("],\"stats\":{\"requestedDays\":"));webSendUInt(days);
  server.sendContent(F(",\"days\":"));webSendUInt(eligible);
  server.sendContent(F(",\"items\":"));webSendUInt(emitted);
  server.sendContent(F(",\"scanned\":"));webSendUInt(scanned);
  server.sendContent(F(",\"duplicatesSkipped\":"));webSendUInt(duplicatesSkipped);
  server.sendContent(F(",\"anchorDay\":"));webSendUInt(anchorDay);
  server.sendContent(F(",\"oldestDay\":"));webSendUInt(haveBounds?oldestRecord.dayKey:0);
  server.sendContent(F(",\"newestDay\":"));webSendUInt(haveBounds?newestRecord.dayKey:0);
  server.sendContent(F(",\"clockAnchor\":"));
  server.sendContent(anchorFromClock?F("true"):F("false"));
  server.sendContent(F(",\"consumptionLiters\":"));webSendFloat(totalC,1);
  server.sendContent(F(",\"refillLiters\":"));webSendFloat(totalR,1);
  server.sendContent(F(",\"minPercent\":"));
  if(minP<=100)webSendFloat(minP,1);else server.sendContent(F("null"));
  server.sendContent(F(",\"maxPercent\":"));
  if(maxP>=0)webSendFloat(maxP,1);else server.sendContent(F("null"));
  server.sendContent(F("}}"));

  server.sendContent("");
  webFinishConnection();

  historyApiLastMs=millis()-apiStartMs;
  historyApiLastItems=emitted;

  Serial.print(F("[HISTORY API] reqDays="));Serial.print(days);
  Serial.print(F(" actualDays="));Serial.print(eligible);
  Serial.print(F(" oldest="));Serial.print(haveBounds?oldestRecord.dayKey:0);
  Serial.print(F(" newest="));Serial.print(haveBounds?newestRecord.dayKey:0);
  Serial.print(F(" pivot="));Serial.print(oldestLogical);
  Serial.print(F(" step="));Serial.print(step);
  Serial.print(F(" dupSkip="));Serial.print(duplicatesSkipped);
  Serial.print(F(" items="));Serial.print(emitted);
  Serial.print(F(" time="));Serial.print(historyApiLastMs);
  Serial.println(F(" ms"));
}

void handleClimateHistoryApi(){
  uint16_t days=365;
  if(server.hasArg("days")){
    long d=server.arg("days").toInt();
    if(d>0&&d<=3650)days=(uint16_t)d;
  }

  if(!historyReady){
    server.send(503,"application/json",
      "{\"ok\":false,\"error\":\"history_not_ready\",\"items\":[]}");
    return;
  }

  File f=LittleFS.open(HISTORY_FILE,"r");
  if(!f){
    server.send(500,"application/json",
      "{\"ok\":false,\"error\":\"history_file_open_failed\",\"items\":[]}");
    return;
  }

  DailyHistoryRecord oldestRecord{},newestRecord{};
  uint32_t oldestLogical=0,newestLogical=0;
  const bool haveBounds=historyChronologicalBoundsFromOpenFile(
    f,oldestRecord,oldestLogical,newestRecord,newestLogical);

  uint32_t anchorDay=0;
  if(!historyDateNow(anchorDay) && haveBounds)anchorDay=newestRecord.dayKey;
  else if(haveBounds && newestRecord.dayKey>anchorDay)anchorDay=newestRecord.dayKey;

  const uint32_t first=historyFirstDayForAnchor(anchorDay,days);

  auto logicalAtChronologicalOffset=[&](uint32_t offset)->uint32_t{
    if(historyHeader.count==0)return 0;
    return (oldestLogical+offset)%historyHeader.count;
  };

  uint32_t climateDays=0;
  DailyHistoryRecord pending{};
  bool havePending=false;

  auto countPending=[&]()->void{
    if(!havePending)return;
    if(first && pending.dayKey<first)return;
    if(anchorDay && pending.dayKey>anchorDay)return;
    if(pending.climateSamples==0)return;
    if(pending.tempAvgHalfC==255 && pending.humidityAvgPct==255)return;
    climateDays++;
  };

  for(uint32_t off=0;off<historyHeader.count;off++){
    const uint32_t li=logicalAtChronologicalOffset(off);
    DailyHistoryRecord r;
    if(!historyReadChronologicalFromOpenFile(f,li,r)){
      if((off&0x7F)==0)yield();
      continue;
    }

    if(!havePending){
      pending=r;havePending=true;
    }else if(r.dayKey==pending.dayKey){
      pending=r;
    }else{
      countPending();
      pending=r;
    }

    if((off&0x7F)==0)yield();
  }
  countPending();

  const uint32_t maxPoints=365;
  uint32_t step=(climateDays+maxPoints-1U)/maxPoints;
  if(step<1U)step=1U;

  webPrepareConnectionClose();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200,"application/json; charset=utf-8","");

  String out;
  out.reserve(384);
  out=F("{\"ok\":true,\"items\":[");

  bool comma=false;
  uint32_t ordinal=0,emitted=0;
  havePending=false;

  auto emitPending=[&]()->void{
    if(!havePending)return;
    if(first && pending.dayKey<first)return;
    if(anchorDay && pending.dayKey>anchorDay)return;
    if(pending.climateSamples==0)return;
    if(pending.tempAvgHalfC==255 && pending.humidityAvgPct==255)return;

    const bool emit=(ordinal%step)==0 || pending.dayKey==anchorDay;
    ordinal++;
    if(!emit)return;

    const time_t ts=historyDayKeyToTime(pending.dayKey);
    if(ts==(time_t)-1 || ts<=0)return;

    char num[24];
    if(comma)out+=',';
    comma=true;

    snprintf(num,sizeof(num),"%llu",
      (unsigned long long)((uint64_t)(uint32_t)ts*1000ULL));
    out+=F("{\"time\":");out+=num;

    out+=F(",\"samples\":");out+=String(pending.climateSamples);

    out+=F(",\"tAvg\":");
    if(pending.tempAvgHalfC!=255)out+=String(historyDecodeTempHalfC(pending.tempAvgHalfC),1);
    else out+=F("null");

    out+=F(",\"tMin\":");
    if(pending.tempMinHalfC!=255)out+=String(historyDecodeTempHalfC(pending.tempMinHalfC),1);
    else out+=F("null");

    out+=F(",\"tMax\":");
    if(pending.tempMaxHalfC!=255)out+=String(historyDecodeTempHalfC(pending.tempMaxHalfC),1);
    else out+=F("null");

    out+=F(",\"hAvg\":");
    if(pending.humidityAvgPct!=255)out+=String(pending.humidityAvgPct);
    else out+=F("null");

    out+=F(",\"hMin\":");
    if(pending.humidityMinPct!=255)out+=String(pending.humidityMinPct);
    else out+=F("null");

    out+=F(",\"hMax\":");
    if(pending.humidityMaxPct!=255)out+=String(pending.humidityMaxPct);
    else out+=F("null");

    out+='}';
    emitted++;

    if(out.length()>300){
      server.sendContent(out);
      out="";
      yield();
    }
  };

  for(uint32_t off=0;off<historyHeader.count;off++){
    const uint32_t li=logicalAtChronologicalOffset(off);
    DailyHistoryRecord r;
    if(!historyReadChronologicalFromOpenFile(f,li,r)){
      if((off&0x7F)==0)yield();
      continue;
    }

    if(!havePending){
      pending=r;havePending=true;
    }else if(r.dayKey==pending.dayKey){
      pending=r;
    }else{
      emitPending();
      pending=r;
    }
    if((off&0x7F)==0)yield();
  }
  emitPending();
  f.close();

  out+=F("],\"days\":");out+=String(climateDays);
  out+=F(",\"itemsCount\":");out+=String(emitted);
  out+=F("}");

  server.sendContent(out);
  server.sendContent("");
  webFinishConnection();

  Serial.print(F("[CLIMATE API] reqDays="));
  Serial.print(days);
  Serial.print(F(" climateDays="));
  Serial.print(climateDays);
  Serial.print(F(" step="));
  Serial.print(step);
  Serial.print(F(" items="));
  Serial.println(emitted);
}

void handleMonthlyComparisonApi(){
  uint8_t years=5;
  if(server.hasArg("years")){
    int y=server.arg("years").toInt();
    if(y==3||y==5||y==10)years=(uint8_t)y;
  }

  time_t now=time(nullptr);
  if(now<1700000000){
    server.send(503,"application/json",
      "{\"ok\":false,\"error\":\"time_not_ready\",\"years\":[],\"months\":[]}");
    return;
  }

  struct tm nt;
  localtime_r(&now,&nt);
  const int currentYear=nt.tm_year+1900;
  const int firstYear=currentYear-(int)years+1;

  uint32_t sums[10][12] = {};
  uint16_t counts[10][12] = {};
  uint32_t duplicatesSkipped=0;
  uint32_t uniqueDays=0;

  if(historyReady && historyHeader.count){
    File f=LittleFS.open(HISTORY_FILE,"r");
    if(!f){
      server.send(500,"application/json",
        "{\"ok\":false,\"error\":\"history_file_open_failed\",\"years\":[],\"months\":[]}");
      return;
    }

    DailyHistoryRecord pending{};
    bool havePending=false;

    auto addMonthlyRecord=[&](const DailyHistoryRecord& r)->void{
      const int y=(int)(r.dayKey/10000UL);
      const int m=(int)((r.dayKey/100UL)%100UL);

      if(y<firstYear||y>currentYear||m<1||m>12)return;

      const uint8_t yi=(uint8_t)(y-firstYear);
      sums[yi][m-1]+=r.consumptionLiters;
      counts[yi][m-1]++;
      uniqueDays++;
    };

    for(uint32_t i=0;i<historyHeader.count;i++){
      DailyHistoryRecord r;
      if(!historyReadChronologicalFromOpenFile(f,i,r)){
        if((i&0x7F)==0)yield();
        continue;
      }

      if(!havePending){
        pending=r;
        havePending=true;
      }else if(r.dayKey==pending.dayKey){
        pending=r;
        duplicatesSkipped++;
      }else{
        addMonthlyRecord(pending);
        pending=r;
      }

      if((i&0x7F)==0)yield();
    }

    if(havePending)addMonthlyRecord(pending);
    f.close();
  }

  webPrepareConnectionClose();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200,"application/json; charset=utf-8","");

  String out;
  out.reserve(480);
  out=F("{\"ok\":true,\"firstYear\":");
  out+=String(firstYear);
  out+=F(",\"currentYear\":");
  out+=String(currentYear);
  out+=F(",\"years\":[");

  for(uint8_t yi=0;yi<years;yi++){
    if(yi)out+=',';
    out+=String(firstYear+yi);
  }

  out+=F("],\"months\":[");
  for(uint8_t m=0;m<12;m++){
    if(m)out+=',';
    out+='[';

    for(uint8_t yi=0;yi<years;yi++){
      if(yi)out+=',';
      if(counts[yi][m]==0)out+=F("null");
      else out+=String(sums[yi][m]);
    }

    out+=']';

    if(out.length()>400){
      server.sendContent(out);
      out="";
      yield();
    }
  }

  out+=F("]}");
  server.sendContent(out);
  server.sendContent("");
  webFinishConnection();

  Serial.print(F("[MONTHLY] Vergleich "));
  Serial.print(years);
  Serial.print(F(" Jahre "));
  Serial.print(firstYear);
  Serial.print('-');
  Serial.print(currentYear);
  Serial.print(F(" unique="));
  Serial.print(uniqueDays);
  Serial.print(F(" dupSkip="));
  Serial.println(duplicatesSkipped);
}

void handleHistoryCsv(){
  uint16_t days=3650;
  if(server.hasArg("days")){
    long d=server.arg("days").toInt();
    if(d>0&&d<=3650)days=(uint16_t)d;
  }

  uint32_t first=historyFirstDayForDays(days),today=0;
  if(!historyDateNow(today) && historyReady && historyHeader.count>0){
    File af=LittleFS.open(HISTORY_FILE,"r");
    if(af){
      DailyHistoryRecord newest;
      uint32_t newestLogical=0;
      if(historyNewestRecordFromOpenFile(af,newest,newestLogical))today=newest.dayKey;
      af.close();
    }
  }

  webPrepareConnectionClose();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Content-Disposition","attachment; filename=fuellstand_history.csv");
  server.send(200,"text/csv; charset=utf-8","");
  server.sendContent(F("Datum;Fuellstand_L;Fuellstand_%25;Verbrauch_L;Nachfuellung_L;Quelle;Temp_Mittel_C;Temp_Min_C;Temp_Max_C;RH_Mittel_%25;RH_Min_%25;RH_Max_%25;Klima_Samples\r\n"));

  File f=LittleFS.open(HISTORY_FILE,"r");
  if(!f){server.sendContent("");return;}

  DailyHistoryRecord pending{};
  bool havePending=false;
  uint32_t duplicatesSkipped=0,exported=0;

  auto appendCsvRecord=[&](const DailyHistoryRecord& r)->void{
    char line[192];
    char tAvg[12]="",tMin[12]="",tMax[12]="";
    char hAvg[8]="",hMin[8]="",hMax[8]="",samples[8]="";

    if(r.climateSamples&&r.tempAvgHalfC!=255)dtostrf(historyDecodeTempHalfC(r.tempAvgHalfC),0,1,tAvg);
    if(r.climateSamples&&r.tempMinHalfC!=255)dtostrf(historyDecodeTempHalfC(r.tempMinHalfC),0,1,tMin);
    if(r.climateSamples&&r.tempMaxHalfC!=255)dtostrf(historyDecodeTempHalfC(r.tempMaxHalfC),0,1,tMax);
    if(r.climateSamples&&r.humidityAvgPct!=255)snprintf(hAvg,sizeof(hAvg),"%u",(unsigned)r.humidityAvgPct);
    if(r.climateSamples&&r.humidityMinPct!=255)snprintf(hMin,sizeof(hMin),"%u",(unsigned)r.humidityMinPct);
    if(r.climateSamples&&r.humidityMaxPct!=255)snprintf(hMax,sizeof(hMax),"%u",(unsigned)r.humidityMaxPct);
    if(r.climateSamples)snprintf(samples,sizeof(samples),"%u",(unsigned)r.climateSamples);

    const uint32_t dk=r.dayKey;
    const unsigned y=(unsigned)(dk/10000UL);
    const unsigned m=(unsigned)((dk/100UL)%100UL);
    const unsigned d=(unsigned)(dk%100UL);

    const int n=snprintf(
      line,sizeof(line),
      "%02u.%02u.%04u;%u;%.1f;%u;%u;%u;%s;%s;%s;%s;%s;%s;%s\r\n",
      d,m,y,
      (unsigned)r.levelLiters,
      (double)historyPercentForLiters(r.levelLiters),
      (unsigned)r.consumptionLiters,
      (unsigned)r.refillLiters,
      (unsigned)r.source,
      tAvg,tMin,tMax,hAvg,hMin,hMax,samples
    );

    if(n>0 && (size_t)n<sizeof(line))server.sendContent(line);
    exported++;
    yield();
  };

  for(uint32_t i=0;i<historyHeader.count;i++){
    DailyHistoryRecord r;
    if(!historyReadChronologicalFromOpenFile(f,i,r))continue;
    if(first&&r.dayKey<first)continue;
    if(today&&r.dayKey>today)continue;

    if(!havePending){
      pending=r;
      havePending=true;
    }else if(r.dayKey==pending.dayKey){
      pending=r;
      duplicatesSkipped++;
    }else{
      appendCsvRecord(pending);
      pending=r;
    }

    if((i & 0x3F) == 0) yield();
  }

  if(havePending)appendCsvRecord(pending);

  Serial.print(F("[HISTORY CSV] exported="));
  Serial.print(exported);
  Serial.print(F(" duplicatesSkipped="));
  Serial.println(duplicatesSkipped);

  f.close();
  server.sendContent("");
  webFinishConnection();
}

void handleRecentRefills(){
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200,"application/json","");

  String j="{\"items\":[";
  uint8_t n=0;
  uint32_t lastDay=0;

  File f=LittleFS.open(HISTORY_FILE,"r");
  if(f){
    for(int32_t i=(int32_t)historyHeader.count-1;i>=0&&n<5;i--){
      DailyHistoryRecord r;
      if(!historyReadChronologicalFromOpenFile(f,(uint32_t)i,r))continue;

      if(r.dayKey==lastDay)continue;
      lastDay=r.dayKey;

      if(!r.refillLiters)continue;

      if(n++)j+=',';
      j+="{\"date\":\""+historyDateString(r.dayKey)+
         "\",\"liters\":"+String(r.refillLiters)+
         ",\"percent\":"+String(historyPercentForLiters(r.levelLiters),1)+"}";

      if ((i & 0x3F) == 0) yield();
    }
    f.close();
  }

  j+="]}";
  server.sendContent(j);
  server.sendContent("");
  webFinishConnection();
}

bool historyResetFile(){
  if(!historyReady)return false;
  historyInvalidateStatsCache();
  LittleFS.remove(HISTORY_FILE);
  historyReady=false;historyCurrentValid=false;
  historySetupAfterFilesystem();
  return historyReady;
}

int32_t historyDayOrdinal(uint32_t dayKey){
  int32_t y=(int32_t)(dayKey/10000UL);
  int32_t m=(int32_t)((dayKey/100UL)%100UL);
  int32_t d=(int32_t)(dayKey%100UL);
  if(y<1970||m<1||m>12||d<1||d>31)return -1;

  if(m<=2){y--;m+=12;}
  return 365*y + y/4 - y/100 + y/400 + (153*(m-3)+2)/5 + d - 1;
}

bool historyAppendExternal(uint32_t dayKey,uint16_t liters,uint16_t cons,uint16_t refill,uint8_t source){
  int32_t existing=historyFindDay(dayKey);
  DailyHistoryRecord r={};
  historyClimateClear(r);
  r.dayKey=dayKey;r.samples=1;r.levelLiters=liters;
  uint16_t p=(uint16_t)constrain((int)lroundf(historyPercentForLiters(liters)*10.0f),0,1000);
  r.avgPermille=r.minPermille=r.maxPermille=p;r.firstLiters=liters;
  r.consumptionLiters=cons;r.refillLiters=refill;r.source=source<=HISTORY_TEST?source:HISTORY_IMPORTED;
  if(existing>=0)return historyWriteRecordAt((uint32_t)existing,r,false);
  uint32_t idx=historyHeader.writeIndex;
  if(historyHeader.count<historyHeader.capacity)historyHeader.count++;
  historyHeader.writeIndex=(historyHeader.writeIndex+1)%historyHeader.capacity;
  return historyWriteRecordAt(idx,r,true);
}

bool historyGenerateFast(uint16_t days) {
  if (!historyReady || days == 0) return false;
  if (days > historyHeader.capacity) days = (uint16_t)min((uint32_t)65535, historyHeader.capacity);

  time_t now = time(nullptr);
  if (now < 1700000000) {
    Serial.println(F("[HISTORY] Testdaten FEHLER: keine gueltige Uhrzeit"));
    return false;
  }

  LittleFS.remove(HISTORY_FILE);

  memset(&historyHeader, 0, sizeof(historyHeader));
  historyHeader.magic = HISTORY_MAGIC;
  historyHeader.version = HISTORY_VERSION;
  historyHeader.recordSize = sizeof(DailyHistoryRecord);

  if (!LittleFS.info(fsInfoCache)) {
    Serial.println(F("[HISTORY] Testdaten FEHLER: LittleFS Info"));
    return false;
  }

  uint32_t usable =
    fsInfoCache.totalBytes > HISTORY_RESERVE_BYTES
      ? fsInfoCache.totalBytes - HISTORY_RESERVE_BYTES
      : 0;

  historyHeader.capacity =
    usable > sizeof(HistoryHeader)
      ? (usable - sizeof(HistoryHeader)) / sizeof(DailyHistoryRecord)
      : 0;

  if (days > historyHeader.capacity) days = (uint16_t)historyHeader.capacity;

  historyHeader.count = days;
  historyHeader.writeIndex = days % historyHeader.capacity;
  historyHeader.crc = historyHeaderCrc(historyHeader);

  File f = LittleFS.open(HISTORY_FILE, "w+");
  if (!f) {
    Serial.println(F("[HISTORY] Testdaten FEHLER: Datei oeffnen"));
    historyReady = false;
    return false;
  }

  if (f.write(reinterpret_cast<const uint8_t*>(&historyHeader),
              sizeof(historyHeader)) != sizeof(historyHeader)) {
    f.close();
    Serial.println(F("[HISTORY] Testdaten FEHLER: Header"));
    historyReady = false;
    return false;
  }

  struct tm tt;
  localtime_r(&now, &tt);
  tt.tm_hour = 12;
  tt.tm_min = 0;
  tt.tm_sec = 0;

  const time_t end = mktime(&tt);
  const time_t start = end - (time_t)(days - 1) * 86400;

  float cap = max(100.0f, historyCapacityLiters());
  float level = cap * 0.90f;
  uint32_t refills = 0;

  Serial.print(F("[HISTORY] Erzeuge Testdaten sequentiell: "));
  Serial.print(days);
  Serial.println(F(" Tage"));

  for (uint16_t i = 0; i < days; ++i) {
    const time_t ts = start + (time_t)i * 86400;
    struct tm d;
