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
    Serial.println(F(LTXT_LOG_FS_MOUNT_ERROR));
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

  Serial.print(F(LTXT_LOG_HISTORY_MIGRATION_DONE));
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
  // Mittag->Mittag: auch Sommer-/Winterzeitwechsel tolerieren.
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
    // Test data is synthetic and must never provide the real daily baseline
    // for consumption/refill calculations. Imported data, however, is considered
    // real history and remains valid as a baseline.
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

    Serial.print(F(LTXT_LOG_DAY_START_PREVIOUS));
    Serial.print(historyDateString(prev.dayKey));
    Serial.print(F(" = "));
    Serial.print(historyDayBaselineLiters);
    Serial.println(F(" L"));
  }else{
    Serial.println(F(LTXT_LOG_NO_CONTIG_PREVIOUS));
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
  // DUPLICATE-DAY GUARD:
  // Before reserving a new ring-buffer slot, always check whether this
  // calendar day already exists. This prevents duplicate days after reboot,
  // test-data generation, or import.
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
      Serial.print(F(LTXT_LOG_REFILL_CONFIRMED));
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
    // Small positive fluctuations below 150 L are not treated as a refill
    // and are not counted as negative consumption either.
    const int32_t cons=start-end;
    r.consumptionLiters=(uint16_t)constrain((int)max((int32_t)0,cons),0,65535);
  }
}

void historyPromoteCurrentTestDayToMeasured(){
  if(!historyCurrentValid || historyCurrent.source!=HISTORY_TEST)return;

  const uint32_t dayKey=historyCurrent.dayKey;
  const uint32_t physicalIndex=historyCurrentIndex;

  Serial.print(F(LTXT_LOG_TEST_TO_REAL));
  Serial.println(historyDateString(dayKey));

  // Recalculate the real baseline. Test data from the previous day is
  // historyFindPreviousDay() bewusst ignoriert.
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

  // The first subsequent real measurement sets firstLiters automatically
  // if no real baseline exists. No refill is derived from the previous test value.
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
  Serial.println(ok?F(" OK"):F(LTXT_LOG_ERROR_SUFFIX));
}

void historyOnMeasurement(){
  if(!historyReady||!isfinite(tankPercent)||!isfinite(tankLiters))return;
  uint32_t dayKey=0;
  if(!historyDateNow(dayKey)){historyTimeValid=false;return;}
  historyTimeValid=true;
  if(!historyCurrentValid)historyStartNewDay(dayKey);
  else if(historyCurrent.dayKey!=dayKey){historyCheckpoint(true);historyStartNewDay(dayKey);}

  // A current-day test record continued during boot must not be mixed with real
  // measurements. The same physical daily record is cleanly converted
  // to measured data without creating a second day.
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

// Fast import uses the same compact file-index format as the

// Explicit C++ forward declarations replacing Arduino auto-prototypes.
bool historyDateNow(uint32_t& dayKey);
bool historyNewestRecordFromOpenFile(File& f, DailyHistoryRecord& r, uint32_t& logicalIndex);
uint32_t historyFirstDayForAnchor(uint32_t anchorDay, uint16_t days);
uint32_t historyOldestPhysicalIndex();
bool historyReadRecordFromOpenFile(File& f, uint32_t physicalIndex, DailyHistoryRecord& r);
void setupFilesystem();

// History repair: one uint32_t physical record index per calendar day.
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
    Serial.println(F(LTXT_LOG_REPAIR_FILE_UNREADABLE));
    return false;
  }

  // ---------------------------------------------------------------------------
  // Pass 1: determine the valid date range and verify the structure.
  // ---------------------------------------------------------------------------
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

  Serial.print(F(LTXT_LOG_REPAIR_PHASE1_DONE));
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
    Serial.println(F(LTXT_LOG_REPAIR_NO_VALID));
    return false;
  }

  const uint32_t slots=(uint32_t)(maxOrd-minOrd+1);
  Serial.print(F("[HISTORY REPAIR] Index slots="));
  Serial.println(slots);
  yield();

  // Safety limit against a corrupted date range.
  if(slots>50000UL){
    src.close();
    Serial.print(F(LTXT_LOG_REPAIR_DATE_RANGE));
    Serial.println(slots);
    return false;
  }

  LittleFS.remove(HISTORY_REPAIR_INDEX_FILE);
  File idx=LittleFS.open(HISTORY_REPAIR_INDEX_FILE,"w+");
  if(!idx){
    src.close();
    Serial.println(F(LTXT_LOG_REPAIR_INDEX_CREATE));
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

  // ---------------------------------------------------------------------------
  // Pass 2: always remember the LAST valid record for each calendar day.
  // ---------------------------------------------------------------------------
  Serial.println(F(LTXT_LOG_REPAIR_BUILD_INDEX));
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

  Serial.print(F(LTXT_LOG_REPAIR_PHASE3_DONE));
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

  Serial.print(F(LTXT_LOG_REPAIR_REQUIRED));
  Serial.print(historyRepairDuplicates);
  Serial.print(F(" invalid="));
  Serial.print(historyRepairInvalid);
  Serial.print(F(" outOfOrder="));
  Serial.println(historyRepairOutOfOrder);

  // ---------------------------------------------------------------------------
  // Pass 3: rebuild the compact history in chronological order.
  // ---------------------------------------------------------------------------
  Serial.println(F(LTXT_LOG_REPAIR_WRITE_COMPACT));
  yield();

  LittleFS.remove(HISTORY_REPAIR_TMP_FILE);
  File tmp=LittleFS.open(HISTORY_REPAIR_TMP_FILE,"w+");
  if(!tmp){
    src.close();idx.close();
    LittleFS.remove(HISTORY_REPAIR_INDEX_FILE);
    Serial.println(F(LTXT_LOG_REPAIR_TEMP_CREATE));
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

  // ---------------------------------------------------------------------------
  // Replace the original only now.
  // ---------------------------------------------------------------------------
  Serial.println(F(LTXT_LOG_REPAIR_SWAP_FILES));
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

  Serial.print(F(LTXT_LOG_REPAIR_DONE_OLD));
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
    Serial.println(F(LTXT_LOG_HISTORY_DISABLED_FS));
    return;
  }

  uint32_t usable=fsInfoCache.totalBytes>HISTORY_RESERVE_BYTES
    ?fsInfoCache.totalBytes-HISTORY_RESERVE_BYTES:0;
  uint32_t capacity=usable>sizeof(HistoryHeader)
    ?(usable-sizeof(HistoryHeader))/sizeof(DailyHistoryRecord):0;

  if(capacity<365){
    Serial.println(F(LTXT_LOG_HISTORY_DISABLED_SPACE));
    return;
  }

  // Migrate V2 (24 B/day) once and atomically to V3 (32 B/day + climate).
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
          Serial.println(F(LTXT_LOG_HISTORY_MIGRATION_ERROR));
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
    if(!f){Serial.println(F(LTXT_LOG_HISTORY_CREATE_ERROR));return;}
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
    Serial.println(F(LTXT_LOG_HISTORY_NEW_V3));
  }else{
    Serial.println(F(LTXT_LOG_HISTORY_EXISTING_V3));
  }

  historyReady=true;
  Serial.println(F(LTXT_LOG_HISTORY_FAST_READ));
  Serial.println(F("[HISTORY] Duplicate-Day Guard aktiv"));
  Serial.print(F("[HISTORY] version="));Serial.print(HISTORY_VERSION);
  Serial.print(F(" record="));Serial.print(sizeof(DailyHistoryRecord));
  Serial.print(F(" B capacity="));Serial.print(historyHeader.capacity);
  Serial.print(F(LTXT_LOG_DAYS_OPEN));Serial.print((float)historyHeader.capacity/365.25f,1);
  Serial.print(F(LTXT_LOG_YEARS_COUNT));Serial.println(historyHeader.count);
  Serial.println(F(LTXT_LOG_HISTORY_CLIMATE));
  Serial.println(F(LTXT_LOG_HISTORY_INTEGRITY_MANUAL));
}

void historySetupTime(){
  if(WiFi.status()!=WL_CONNECTED){Serial.println(F(LTXT_LOG_NTP_WAITS_WIFI));return;}
  setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1);tzset();
  configTime(0,0,"pool.ntp.org","time.nist.gov");
  Serial.println(F("[TIME] NTP gestartet"));
  uint32_t start=millis();
  while(time(nullptr)<1700000000&&millis()-start<5000UL){delay(100);yield();}
  uint32_t dayKey=0;
  if(historyDateNow(dayKey)){
    historyTimeValid=true;Serial.print(F(LTXT_LOG_DATE_PREFIX));Serial.println(historyDateString(dayKey));
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

  // Prefer the current system time.
  uint32_t anchorDay=0;
  if(historyDateNow(anchorDay)){
    return historyFirstDayForAnchor(anchorDay,days);
  }

  // Fallback for AP operation without NTP:
  // use the newest available history day as the time anchor.
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
    // For the same day, the record stored later wins.
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

  // After a legacy import, the file may be physically rotated, e.g.
  // TODAY -> OLD IMPORT -> ... -> YESTERDAY. oldestLogical points to the
  // actual oldest calendar day. Reading cyclically from there restores
  // chronological order without rewriting the file.
  auto logicalAtChronologicalOffset=[&](uint32_t offset)->uint32_t{
    if(historyHeader.count==0)return 0;
    return (oldestLogical+offset)%historyHeader.count;
  };

  // Pass 1: count unique existing days in the requested window.
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
      pending=r; // the last record of this day wins
      duplicatesSkipped++;
    }else{
      countPending();
      pending=r;
    }
    if((scanned&0x7F)==0)yield();
  }
  countPending();

  // Downsampling is based on the ACTUALLY available days.
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

  // Pass 2: output the same cyclic order.
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

  // Count only days with climate data; at most about 365 output points.
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

  // 10 years x 12 months = only 120 values on the stack.
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
        // the last record of the day wins
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
      // Zero means there is no daily data for this month/year.
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
  Serial.print(F(LTXT_LOG_YEARS_SPACED));
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

      // When reading backwards, the first hit for a day is the newest
      // record for that calendar day. Ignore older duplicates of the same day.
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

// ---------------------------------------------------------------------------
// V0.9.10: restored history/import helper block
// ---------------------------------------------------------------------------
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

  // Gregorian day index, independent of timezone/DST.
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
    Serial.println(F(LTXT_LOG_TEST_NO_TIME));
    return false;
  }

  // Rebuild the existing history in one step.
  LittleFS.remove(HISTORY_FILE);

  memset(&historyHeader, 0, sizeof(historyHeader));
  historyHeader.magic = HISTORY_MAGIC;
  historyHeader.version = HISTORY_VERSION;
  historyHeader.recordSize = sizeof(DailyHistoryRecord);

  if (!LittleFS.info(fsInfoCache)) {
    Serial.println(F(LTXT_LOG_TEST_FS_INFO));
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
    Serial.println(F(LTXT_LOG_TEST_OPEN_FILE));
    historyReady = false;
    return false;
  }

  if (f.write(reinterpret_cast<const uint8_t*>(&historyHeader),
              sizeof(historyHeader)) != sizeof(historyHeader)) {
    f.close();
    Serial.println(F(LTXT_LOG_TEST_HEADER));
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

  Serial.print(F(LTXT_LOG_TEST_GENERATE));
  Serial.print(days);
  Serial.println(F(LTXT_LOG_DAYS_SUFFIX));

  for (uint16_t i = 0; i < days; ++i) {
    const time_t ts = start + (time_t)i * 86400;
    struct tm d;
    localtime_r(&ts, &d);

    float seasonal =
      1.0f + 0.55f *
      cosf((((float)d.tm_yday - 15.0f) / 365.0f) * 6.2831853f);

    float daily =
      cap * 0.0012f * seasonal *
      (1.0f + 0.08f * sinf((float)i * 1.731f));

    daily = constrain(daily, 0.1f, cap * 0.01f);

    uint16_t refill = 0;
    uint16_t cons = (uint16_t)constrain((int)lroundf(daily), 0, 65535);

    level -= daily;

    if (level < cap * 0.25f || (i > 30 && (i % 170) == 0)) {
      float before = level;
      level = min(cap * 0.92f, level + cap * 0.60f);
      refill = (uint16_t)constrain((int)lroundf(max(0.0f, level - before)), 0, 65535);
      cons = 0;
      refills++;
    }

    DailyHistoryRecord r = {};
    historyClimateClear(r);
    r.dayKey = historyDayKeyFromTime(ts);
    r.samples = 1;
    r.levelLiters = (uint16_t)constrain((int)lroundf(level), 0, 65535);

    uint16_t p = (uint16_t)constrain(
      (int)lroundf(historyPercentForLiters(r.levelLiters) * 10.0f),
      0, 1000);

    r.avgPermille = p;
    r.minPermille = p;
    r.maxPermille = p;
    r.firstLiters = r.levelLiters;
    r.consumptionLiters = cons;
    r.refillLiters = refill;
    r.source = HISTORY_TEST;
    r.flags = 0;
    r.crc16 = historyRecordCrc(r);

    if (f.write(reinterpret_cast<const uint8_t*>(&r), sizeof(r)) != sizeof(r)) {
      f.close();
      Serial.print(F(LTXT_LOG_TEST_WRITE_ERROR));
      Serial.println(i);
      historyReady = false;
      return false;
    }

    // Service the ESP8266/WDT, but do not reopen the file for every record.
    if ((i & 0x1F) == 0) {
      yield();

      if ((i % 365) == 0 || i + 1 == days) {
        Serial.print(F(LTXT_LOG_TEST_PROGRESS));
        Serial.print(i + 1);
        Serial.print('/');
        Serial.println(days);
      }
    }
  }

  f.flush();
  f.close();

  historyReady = true;
  historyCurrentValid = false;
  historyWriteCount += days;
  historyInvalidateStatsCache();

  Serial.print(F(LTXT_LOG_TEST_DONE));
  Serial.print(days);
  Serial.print(F(LTXT_LOG_DAYS_REFILLS));
  Serial.println(refills);

  return true;
}

void historyGenerate(uint16_t days) {
  historyGenerateFast(days);
}

void handleGenerateTestHistory(){historyGenerate(365);server.sendHeader("Location","/history",true);server.send(303,"text/plain","");}

void handleGenerate10YearTestHistory(){historyGenerate(3650);server.sendHeader("Location","/history",true);server.send(303,"text/plain","");}

void handleClearHistory(){
  // Two-step safety confirmation:
  // 1) Browser-confirm() im Formular
  // 2) server side must confirm exactly LOESCHEN.
  if(!server.hasArg("confirmText") || server.arg("confirmText")!=LTXT_DELETE_CONFIRM_WORD){
    Serial.println(F(LTXT_LOG_DELETE_ABORTED));

    String html;
    html.reserve(520);
    html=F("<!doctype html><html><head><meta charset='utf-8'>"
           "<meta name='viewport' content='width=device-width,initial-scale=1'>"
           LHTML_HISTORY_NOT_DELETED_TITLE
           "<body style='font-family:sans-serif;background:#111;color:#eee;padding:20px'>"
           LHTML_HISTORY_NOT_DELETED_H2
           "<p>Die Sicherheitsbestaetigung war nicht korrekt.</p>"
           LHTML_DELETE_CONFIRM_P
           "<p><a style='color:#8fd3ff' href='/history/maintenance'>Zur " LTXT_MAINTENANCE "</a></p>"
           "</body></html>");
    server.send(400,"text/html; charset=utf-8",html);
    return;
  }

  Serial.println(F(LTXT_LOG_DELETE_CONFIRMED));
historyResetFile();server.sendHeader("Location","/history",true);server.send(303,"text/plain","");}

bool parseImportDate(String v,uint32_t& dayKey){
  v.trim();char sep=v.indexOf('.')>=0?'.':'-';int p1=v.indexOf(sep),p2=p1>=0?v.indexOf(sep,p1+1):-1;
  if(p1<0||p2<0)return false;
  int d=v.substring(0,p1).toInt(),m=v.substring(p1+1,p2).toInt(),y=v.substring(p2+1).toInt();
  if(y<100)y+=2000;if(d<1||d>31||m<1||m>12||y<2000)return false;
  struct tm t={};t.tm_year=y-1900;t.tm_mon=m-1;t.tm_mday=d;t.tm_hour=12;time_t ts=mktime(&t);
  if(ts==(time_t)-1)return false;struct tm chk;localtime_r(&ts,&chk);
  if(chk.tm_mday!=d||chk.tm_mon!=m-1||chk.tm_year!=y-1900)return false;
  dayKey=(uint32_t)y*10000UL+(uint32_t)m*100UL+(uint32_t)d;return true;
}

HistoryImportParsed parseHistoryImportLine(String line, uint32_t today, uint32_t oldest) {
  HistoryImportParsed p{};
  p.valid=false;
  p.autoDerived=false;
  p.climateValid=false;
  p.source=HISTORY_IMPORTED;
  p.climateSamples=0;

  line.trim();
  if(!line.length()){p.reason=F("Leer");return p;}

  String cols[13];
  uint8_t n=0;
  int pos=0;
  while(n<13){
    int q=line.indexOf(';',pos);
    if(q<0){
      cols[n++]=line.substring(pos);
      break;
    }
    cols[n++]=line.substring(pos,q);
    pos=q+1;
  }

  if(n<2){p.reason=F("Zu wenige Spalten");return p;}

  uint32_t day=0;
  if(!parseImportDate(cols[0],day)){p.reason=F(LTXT_IMPORT_DATE_INVALID);return p;}
  if(today && day>today){p.reason=F(LTXT_IMPORT_DATE_FUTURE);return p;}
  if(oldest && day<oldest){p.reason=F(LTXT_IMPORT_TOO_OLD);return p;}

  cols[1].trim();
  cols[1].replace(',', '.');
  if(!cols[1].length()){p.reason=F("Liter fehlt");return p;}
  float liters=cols[1].toFloat();
  if(!isfinite(liters)||liters<0||liters>65535){p.reason=F(LTXT_IMPORT_LITERS_INVALID);return p;}

  p.dayKey=day;
  p.liters=(uint16_t)constrain((int)lroundf(liters),0,65535);

  const bool hasConsumption=n>=4 && cols[3].length();
  const bool hasRefill=n>=5 && cols[4].length();
  p.autoDerived=!(hasConsumption || hasRefill);

  if(hasConsumption){
    cols[3].replace(',', '.');
    float v=cols[3].toFloat();
    if(isfinite(v)&&v>=0)p.consumption=(uint16_t)constrain((int)lroundf(v),0,65535);
  }

  if(hasRefill){
    cols[4].replace(',', '.');
    float v=cols[4].toFloat();
    if(isfinite(v)&&v>=0)p.refill=(uint16_t)constrain((int)lroundf(v),0,65535);
  }

  if(n>=6){
    cols[5].trim();
    int q=cols[5].toInt();
    if(q>=0&&q<=2)p.source=(uint8_t)q;
  }

  // V3 climate format:
  // 6 TempAvg, 7 TempMin, 8 TempMax, 9 RHAvg, 10 RHMin, 11 RHMax,
  // optional 12 Klima_Samples.
  if(n>=12){
    bool present=true;
    for(uint8_t i=6;i<=11;i++){
      cols[i].trim();
      if(!cols[i].length()){present=false;break;}
      cols[i].replace(',', '.');
    }

    if(present){
      const float ta=cols[6].toFloat();
      const float tn=cols[7].toFloat();
      const float tx=cols[8].toFloat();
      const float ha=cols[9].toFloat();
      const float hn=cols[10].toFloat();
      const float hx=cols[11].toFloat();

      const bool plausible=
        isfinite(ta)&&isfinite(tn)&&isfinite(tx)&&
        isfinite(ha)&&isfinite(hn)&&isfinite(hx)&&
        ta>=-40.0f&&ta<=85.0f&&tn>=-40.0f&&tn<=85.0f&&tx>=-40.0f&&tx<=85.0f&&
        ha>=0.0f&&ha<=100.0f&&hn>=0.0f&&hn<=100.0f&&hx>=0.0f&&hx<=100.0f&&
        tn<=ta&&ta<=tx&&hn<=ha&&ha<=hx;

      if(plausible){
        p.climateValid=true;
        p.tempAvgC=ta;
        p.tempMinC=tn;
        p.tempMaxC=tx;
        p.humidityAvgPct=ha;
        p.humidityMinPct=hn;
        p.humidityMaxPct=hx;
        p.climateSamples=1;

        if(n>=13){
          cols[12].trim();
          if(cols[12].length()){
            long cs=cols[12].toInt();
            if(cs>0 && cs<=65535)p.climateSamples=(uint16_t)cs;
          }
        }
      }
    }
  }

  p.valid=true;
  if(p.climateValid){
    p.reason=p.autoDerived?F(LTXT_IMPORT_OK_AUTO_CLIMATE):F(LTXT_IMPORT_OK_CSV_CLIMATE);
  }else{
    p.reason=p.autoDerived?F("OK / automatisch"):F("OK / CSV");
  }
  return p;
}

void historyApplyImportedClimate(const HistoryImportParsed& p, DailyHistoryRecord& r){
  historyClimateClear(r);
  if(!p.climateValid)return;

  r.tempAvgHalfC=historyEncodeTempHalfC(p.tempAvgC);
  r.tempMinHalfC=historyEncodeTempHalfC(p.tempMinC);
  r.tempMaxHalfC=historyEncodeTempHalfC(p.tempMaxC);
  r.humidityAvgPct=historyEncodeHumidity(p.humidityAvgPct);
  r.humidityMinPct=historyEncodeHumidity(p.humidityMinPct);
  r.humidityMaxPct=historyEncodeHumidity(p.humidityMaxPct);
  r.climateSamples=max((uint16_t)1,p.climateSamples);
}

void historyDeriveImportFlow(HistoryImportParsed& p, uint32_t prevDay, uint16_t prevLiters, bool prevValid) {
  if(!p.valid || !p.autoDerived){
    return;
  }

  p.consumption=0;
  p.refill=0;

  if(!prevValid || !historyDaysAreAdjacent(prevDay,p.dayKey)){
    return;
  }

  const int32_t delta=(int32_t)p.liters-(int32_t)prevLiters;

  if(delta >= (int32_t)HISTORY_REFILL_MIN_LITERS){
    p.refill=(uint16_t)min((int32_t)65535,delta);
    p.consumption=0;
  }else if(delta < 0){
    p.consumption=(uint16_t)min((int32_t)65535,-delta);
  }
}

void handleHistoryImportPage(){
  webStreamBegin(F(LTXT_CSV_IMPORT));
  webStreamNav(1);
  server.sendContent(F(
    "<div class='card'><h1>CSV Import</h1>"
    LHTML_IMPORT_COMPAT
    LHTML_IMPORT_FULL_FORMAT
    LHTML_IMPORT_V3
    LHTML_IMPORT_SIMPLE
    LHTML_IMPORT_SIMPLE_HINT_START
    LTXT_IMPORT_SIMPLE_CALC_2
    "<form method='POST' action='/history/import/preview' enctype='multipart/form-data'>"
    "<input type='file' name='data' accept='.csv,text/csv' required>"
    LHTML_IMPORT_CHECK_BUTTON
    LHTML_BACK_HISTORY
  ));
  webStreamEnd();
}

void handleHistoryImportUpload(){
  HTTPUpload& up=server.upload();
  static File f;
  if(up.status==UPLOAD_FILE_START){
    LittleFS.remove(HISTORY_IMPORT_PREVIEW_FILE);
    f=LittleFS.open(HISTORY_IMPORT_PREVIEW_FILE,"w");
  }else if(up.status==UPLOAD_FILE_WRITE){
    if(f)f.write(up.buf,up.currentSize);
  }else if(up.status==UPLOAD_FILE_END||up.status==UPLOAD_FILE_ABORTED){
    if(f)f.close();
  }
}

void handleHistoryImportPreview(){
  File f=LittleFS.open(HISTORY_IMPORT_PREVIEW_FILE,"r");
  if(!f){server.send(400,"text/plain",LTXT_IMPORT_FILE_MISSING);return;}

  uint32_t today=0; historyDateNow(today);
  time_t now=time(nullptr);
  uint32_t oldest=now>1700000000?historyDayKeyFromTime(now-(time_t)3650*86400):0;

  uint32_t ok=0,bad=0,total=0,shown=0,autoRows=0,climateRows=0;
  uint32_t prevDay=0;
  uint16_t prevLiters=0;
  bool prevValid=false;

  // First pass: count only. No large RAM buffers.
  while(f.available()){
    String line=f.readStringUntil('\n');line.trim();
    if(!line.length())continue;
    if(line.startsWith(LTXT_DATE)||line.startsWith(LTXT_DATE_LOWER))continue;

    total++;
    HistoryImportParsed p=parseHistoryImportLine(line,today,oldest);
    if(p.valid){
      historyDeriveImportFlow(p,prevDay,prevLiters,prevValid);
      ok++;
      if(p.autoDerived)autoRows++;
      if(p.climateValid)climateRows++;
      prevDay=p.dayKey;
      prevLiters=p.liters;
      prevValid=true;
    }else{
      bad++;
    }

    if((total&0x3F)==0)yield();
  }

  f.seek(0,SeekSet);
  prevDay=0;prevLiters=0;prevValid=false;

  webStreamBegin(F(LTXT_IMPORT_PREVIEW));
  webStreamNav(1);

  server.sendContent(F("<div class='card'><h1>CSV Import – Vorschau</h1><div class='grid'>"));

  String tiny;
  tiny.reserve(220);
  tiny=F("<div class='metric-card'><h3>Zeilen</h3><div>");
  tiny+=String(total);tiny+=F(LHTML_IMPORT_VALID_CARD);
  tiny+=String(ok);tiny+=F("</div></div><div class='metric-card'><h3>Verworfen</h3><div class='bad'>");
  tiny+=String(bad);tiny+=F("</div></div><div class='metric-card'><h3>Automatisch berechnet</h3><div>");
  tiny+=String(autoRows);tiny+=F(LHTML_IMPORT_CLIMATE_CARD);
  tiny+=String(climateRows);tiny+=F("</div></div></div>");
  server.sendContent(tiny);

  server.sendContent(F(
    LHTML_IMPORT_GAPS_HINT_START
    LTXT_IMPORT_GAPS_HINT_2
    LHTML_IMPORT_TABLE_HEADER
  ));

  uint32_t rowNo=0;
  while(f.available() && shown<100){
    String line=f.readStringUntil('\n');line.trim();
    if(!line.length())continue;
    if(line.startsWith(LTXT_DATE)||line.startsWith(LTXT_DATE_LOWER))continue;

    rowNo++;
    HistoryImportParsed p=parseHistoryImportLine(line,today,oldest);

    if(p.valid){
      historyDeriveImportFlow(p,prevDay,prevLiters,prevValid);
      prevDay=p.dayKey;
      prevLiters=p.liters;
      prevValid=true;
    }

    String row;
    row.reserve(280);
    row+=F("<tr><td>");row+=String(rowNo);row+=F("</td><td>");
    row+=p.valid?historyDateString(p.dayKey):F("--");
    row+=F("</td><td>");row+=p.valid?String(p.liters):F("--");
    row+=F("</td><td>");row+=p.valid?String(p.consumption):F("--");
    row+=F("</td><td>");row+=p.valid?String(p.refill):F("--");
    row+=F("</td><td>");
    if(p.valid)row+=(p.source==HISTORY_MEASURED?F("Gemessen"):(p.source==HISTORY_TEST?F("Test"):F("Import")));
    else row+=F("--");
    row+=F("</td><td>");
    if(p.valid&&p.climateValid){
      row+=String(p.tempAvgC,1);row+=F(" C / ");row+=String(p.humidityAvgPct,0);row+=F(" %");
    }else row+=F("--");
    row+=F("</td><td>");
    row+=p.valid?(p.autoDerived?F("automatisch"):F("aus CSV")):p.reason;
    row+=F("</td><td>");
    row+=p.valid?F("<span class='ok'>OK</span>"):F("<span class='bad'>Verworfen</span>");
    row+=F("</td></tr>");
    server.sendContent(row);

    shown++;
    if((shown&0x0F)==0)yield();
  }

  f.close();

  server.sendContent(F("</table></div>"));
  if(total>shown){
    server.sendContent(F(LHTML_IMPORT_FIRST100));
  }

  server.sendContent(F("<div class='links' style='margin-top:14px'>"));
  if(ok){
    server.sendContent(F(LHTML_IMPORT_APPLY_FORM));
  }
  server.sendContent(F("<form method='POST' action='/history/import/cancel' style='margin:0'><button class='danger' type='submit'>Abbrechen</button></form></div></div>"));
  webStreamEnd();
}

void handleHistoryImportApply(){
  File preview=LittleFS.open(HISTORY_IMPORT_PREVIEW_FILE,"r");
  if(!preview){server.send(400,"text/plain",LTXT_IMPORT_PREVIEW_MISSING);return;}

  uint32_t today=0; historyDateNow(today);
  time_t now=time(nullptr);
  uint32_t oldest=now>1700000000?historyDayKeyFromTime(now-(time_t)3650*86400):0;

  // ---------------------------------------------------------------------------
  // Pass 1: determine the range of valid import days.
  // This means the temporary index only needs to cover this exact date range.
  // ---------------------------------------------------------------------------
  int32_t minOrdinal=INT32_MAX;
  int32_t maxOrdinal=INT32_MIN;
  uint32_t previewValid=0;

  while(preview.available()){
    String line=preview.readStringUntil('\n');line.trim();
    if(!line.length())continue;
    if(line.startsWith(LTXT_DATE)||line.startsWith(LTXT_DATE_LOWER))continue;

    HistoryImportParsed p=parseHistoryImportLine(line,today,oldest);
    if(!p.valid)continue;

    int32_t ord=historyDayOrdinal(p.dayKey);
    if(ord<0)continue;
    if(ord<minOrdinal)minOrdinal=ord;
    if(ord>maxOrdinal)maxOrdinal=ord;
    previewValid++;

    if((previewValid&0x3F)==0)yield();
  }

  if(previewValid==0||minOrdinal>maxOrdinal){
    preview.close();
    LittleFS.remove(HISTORY_IMPORT_PREVIEW_FILE);
    server.send(400,"text/plain",LTXT_IMPORT_NO_VALID);
    return;
  }

  const uint32_t slots=(uint32_t)(maxOrdinal-minOrdinal+1);
  preview.seek(0,SeekSet);

  LittleFS.remove(HISTORY_IMPORT_INDEX_FILE);
  File idxFile=LittleFS.open(HISTORY_IMPORT_INDEX_FILE,"w+");
  if(!idxFile){
    preview.close();
    server.send(500,"text/plain","Import-Index konnte nicht erstellt werden");
    return;
  }

  if(!historyImportIndexCreate(idxFile,slots)){
    preview.close();idxFile.close();
    LittleFS.remove(HISTORY_IMPORT_INDEX_FILE);
    server.send(500,"text/plain","Import-Index Initialisierung fehlgeschlagen");
    return;
  }

  File hist=LittleFS.open(HISTORY_FILE,"r+");
  if(!hist){
    preview.close();idxFile.close();
    LittleFS.remove(HISTORY_IMPORT_INDEX_FILE);
    server.send(500,"text/plain",LTXT_IMPORT_HISTORY_OPEN_ERROR);
    return;
  }

  // ---------------------------------------------------------------------------
  // Scan the existing history EXACTLY ONCE and index only relevant days.
  // ---------------------------------------------------------------------------
  const uint32_t oldestPhysical=historyOldestPhysicalIndex();
  uint32_t indexed=0;

  for(uint32_t li=0;li<historyHeader.count;li++){
    const uint32_t physical=(oldestPhysical+li)%historyHeader.capacity;
    DailyHistoryRecord r;
    if(!historyReadRecordFromOpenFile(hist,physical,r))continue;

    const int32_t ord=historyDayOrdinal(r.dayKey);
    if(ord<minOrdinal||ord>maxOrdinal)continue;

    const uint32_t slot=(uint32_t)(ord-minOrdinal);
    if(historyImportIndexWrite(idxFile,slot,physical))indexed++;

    if((li&0x7F)==0)yield();
  }
  idxFile.flush();

  // If no existing history day was found anywhere in the import range,
  // the file index is unnecessary. All import days can then be appended
  // directly and sequentially to the history. On ESP8266 this saves thousands of seek()
  // operations and is many times faster for an initial import.
  // Bulk append is safe only if the existing history is empty or
  // ends chronologically BEFORE the import range. Otherwise older import days would
  // be written physically after newer records and break chronological order.
  bool bulkAppend = false;
  if(indexed == 0){
    if(historyHeader.count == 0){
      bulkAppend = true;
    }else{
      File chronologyFile=LittleFS.open(HISTORY_FILE,"r");
      DailyHistoryRecord newestExisting{};
      uint32_t newestExistingLogical=0;
      if(chronologyFile){
        historyNewestRecordFromOpenFile(chronologyFile,newestExisting,newestExistingLogical);
        chronologyFile.close();
      }
      const int32_t newestExistingOrdinal=historyDayOrdinal(newestExisting.dayKey);
      bulkAppend = (newestExisting.dayKey>0 && newestExistingOrdinal < minOrdinal);
    }
  }

  if(bulkAppend){
    Serial.println(F("[IMPORT] BULK-APPEND aktiv: Chronologie bleibt erhalten"));
  }else if(indexed==0){
    Serial.println(F("[IMPORT] BULK-APPEND gesperrt: bestehende neuere History -> INDEX-Modus"));
  }

  // ---------------------------------------------------------------------------
  // Pass 2: apply the import. For existing days, use O(1) lookup via the index;
  // for an initial import, use direct sequential bulk append without index access.
  // Duplicate days within the same CSV: the later record wins,
  // because the index points to the new record immediately after an append.
  // ---------------------------------------------------------------------------
  const uint32_t importApplyStartMs=millis();
  uint32_t ok=0,bad=0,derived=0,refills=0,updated=0,appended=0;
  uint32_t processed=0;
  uint32_t prevDay=0;
  uint16_t prevLiters=0;
  bool prevValid=false;
  bool headerDirty=false;

  Serial.print(F("[IMPORT] Fast-Apply Start valid="));
  Serial.print(previewValid);
  Serial.print(F(" slots="));
  Serial.print(slots);
  Serial.print(F(" indexed="));
  Serial.println(indexed);

  while(preview.available()){
    String line=preview.readStringUntil('\n');line.trim();
    if(!line.length())continue;
    if(line.startsWith(LTXT_DATE)||line.startsWith(LTXT_DATE_LOWER))continue;

    HistoryImportParsed p=parseHistoryImportLine(line,today,oldest);
    if(!p.valid){bad++;continue;}

    processed++;
    if((processed%250U)==0U || processed==previewValid){
      Serial.print(F(LTXT_LOG_IMPORT_PROGRESS));
      Serial.print(processed);
      Serial.print('/');
      Serial.println(previewValid);
      yield();
    }

    historyDeriveImportFlow(p,prevDay,prevLiters,prevValid);

    if(p.autoDerived){
      derived++;
      if(p.refill>=HISTORY_REFILL_MIN_LITERS)refills++;
    }

    const int32_t ord=historyDayOrdinal(p.dayKey);
    if(ord<minOrdinal||ord>maxOrdinal){
      bad++;
      continue;
    }

    const uint32_t slot=(uint32_t)(ord-minOrdinal);
    uint32_t physical=0xFFFFFFFFUL;

    DailyHistoryRecord r={};
    historyApplyImportedClimate(p,r);
    r.dayKey=p.dayKey;
    r.samples=1;
    r.levelLiters=p.liters;
    const uint16_t permille=(uint16_t)constrain(
      (int)lroundf(historyPercentForLiters(p.liters)*10.0f),0,1000);
    r.avgPermille=r.minPermille=r.maxPermille=permille;
    r.firstLiters=p.liters;
    r.consumptionLiters=p.consumption;
    r.refillLiters=p.refill;
    r.source=p.source<=HISTORY_TEST?p.source:HISTORY_IMPORTED;

    bool writeOk=false;

    if(bulkAppend){
      // Initial import: no index read/write operations. Write directly at the current
      // writeIndex position and update the header only in RAM.
      physical=historyHeader.writeIndex;
      writeOk=historyWriteRecordToOpenFile(hist,physical,r);

      if(writeOk){
        if(historyHeader.count<historyHeader.capacity)historyHeader.count++;
        historyHeader.writeIndex=(historyHeader.writeIndex+1)%historyHeader.capacity;
        headerDirty=true;
        appended++;
      }
    }else{
      if(!historyImportIndexRead(idxFile,slot,physical)){
        bad++;
        continue;
      }

      if(physical!=0xFFFFFFFFUL && physical<historyHeader.capacity){
        writeOk=historyWriteRecordToOpenFile(hist,physical,r);
        if(writeOk)updated++;
      }else{
        physical=historyHeader.writeIndex;
        writeOk=historyWriteRecordToOpenFile(hist,physical,r);

        if(writeOk){
          if(historyHeader.count<historyHeader.capacity)historyHeader.count++;
          historyHeader.writeIndex=(historyHeader.writeIndex+1)%historyHeader.capacity;
          headerDirty=true;
          appended++;
          historyImportIndexWrite(idxFile,slot,physical);
        }
      }
    }

    if(writeOk){
      ok++;
      historyWriteCount++;
    }else{
      bad++;
      historyWriteErrors++;
    }

    prevDay=p.dayKey;
    prevLiters=p.liters;
    prevValid=true;

    if(bulkAppend){
      if(((ok+bad)%500U)==0U){
        hist.flush();
        yield();
      }
    }else if(((ok+bad)&0x3F)==0){
      hist.flush();
      yield();
    }
  }

  bool headerOk=true;
  if(headerDirty)headerOk=historyWriteHeader(hist);
  hist.flush();
  idxFile.flush();

  preview.close();
  hist.close();
  idxFile.close();

  LittleFS.remove(HISTORY_IMPORT_INDEX_FILE);
  LittleFS.remove(HISTORY_IMPORT_PREVIEW_FILE);

  historyInvalidateStatsCache();

  // If the current day was imported, the next measurement cycle will
  // continue the same day cleanly through the duplicate-day guard.
  historyCurrentValid=false;

  Serial.print(F(LTXT_LOG_IMPORT_DONE));Serial.print(processed);
  Serial.print(F(" OK="));Serial.print(ok);
  Serial.print(F(LTXT_LOG_ERRORS_SUFFIX));Serial.print(bad);
  Serial.print(F(" Update="));Serial.print(updated);
  Serial.print(F(" Append="));Serial.print(appended);
  Serial.print(F(" automatisch="));Serial.print(derived);
  Serial.print(F(LTXT_LOG_REFILLS_SUFFIX));Serial.print(refills);
  Serial.print(F(" Header="));Serial.print(headerOk?F("OK"):F(LTXT_ERROR));
  Serial.print(F(" Modus="));Serial.print(bulkAppend?F("BULK"):F("INDEX"));
  Serial.print(F(" Zeit="));Serial.print(millis()-importApplyStartMs);Serial.println(F(" ms"));

  webStreamBegin(F(LTXT_CSV_IMPORT));
  webStreamNav(1);

  String s;
  s.reserve(480);
  s=F("<div class='card'><h1>CSV Import abgeschlossen</h1><div class='grid'>"
      "<div class='metric-card'><h3>Uebernommen</h3><div>");
  s+=String(ok);
  s+=F("</div></div><div class='metric-card'><h3>Verworfen</h3><div>");
  s+=String(bad);
  s+=F("</div></div><div class='metric-card'><h3>Aktualisiert</h3><div>");
  s+=String(updated);
  s+=F("</div></div><div class='metric-card'><h3>Neu</h3><div>");
  s+=String(appended);
  s+=F("</div></div><div class='metric-card'><h3>Automatisch berechnet</h3><div>");
  s+=String(derived);
  s+=F(LHTML_REFILLS_CARD);
  s+=String(refills);
  s+=F("</div></div><div class='metric-card'><h3>Importmodus</h3><div>");
  s+=bulkAppend?F("BULK"):F("INDEX");
  s+=F("</div></div><div class='metric-card'><h3>Dauer</h3><div>");
  s+=String(millis()-importApplyStartMs);
  s+=F(" ms</div></div></div>");
  if(!bulkAppend && indexed==0 && appended>0){
    s+=F("<p style='color:#ffb52e'><b>Hinweis:</b> Ältere Daten wurden zu einer bereits neueren History hinzugefügt. "
         LTXT_IMPORT_REPAIR_HINT);
  }
  if(!headerOk)s+=F(LHTML_HEADER_SAVE_WARNING);
  s+=F("<div class='links' style='margin-top:14px'><a class='btn' href='/history'>Zur Historie</a></div></div>");
  server.sendContent(s);
  webStreamEnd();
}

void handleHistoryImportCancel(){
  LittleFS.remove(HISTORY_IMPORT_PREVIEW_FILE);
  server.sendHeader("Location","/history",true);
  server.send(303,"text/plain","");
}

uint32_t historyCountSource(uint8_t source){
  if(!historyReady || historyHeader.count==0)return 0;

  File f=LittleFS.open(HISTORY_FILE,"r");
  if(!f)return 0;

  const uint32_t oldest=historyOldestPhysicalIndex();
  uint32_t count=0;

  for(uint32_t li=0;li<historyHeader.count;li++){
    const uint32_t physical=(oldest+li)%historyHeader.capacity;
    DailyHistoryRecord r;
    if(historyReadRecordFromOpenFile(f,physical,r) && r.source==source)count++;
    if((li&0x7F)==0)yield();
  }

  f.close();
  return count;
}

bool historyDeleteSource(uint8_t source,uint32_t& removed){
  removed=0;
  if(!historyReady)return false;
  if(historyHeader.count==0)return true;

  File src=LittleFS.open(HISTORY_FILE,"r");
  if(!src)return false;

  LittleFS.remove(HISTORY_FILTER_TMP_FILE);
  File tmp=LittleFS.open(HISTORY_FILTER_TMP_FILE,"w+");
  if(!tmp){
    src.close();
    return false;
  }

  HistoryHeader newHeader=historyHeader;
  newHeader.count=0;
  newHeader.writeIndex=0;
  newHeader.crc=historyHeaderCrc(newHeader);

  if(tmp.write(reinterpret_cast<const uint8_t*>(&newHeader),sizeof(newHeader))!=sizeof(newHeader)){
    src.close();tmp.close();
    LittleFS.remove(HISTORY_FILTER_TMP_FILE);
    return false;
  }

  const uint32_t oldest=historyOldestPhysicalIndex();
  uint32_t kept=0;

  for(uint32_t li=0;li<historyHeader.count;li++){
    const uint32_t physical=(oldest+li)%historyHeader.capacity;
    DailyHistoryRecord r;

    if(!historyReadRecordFromOpenFile(src,physical,r)){
      // Do not copy defective records here without validation.
      removed++;
      continue;
    }

    if(r.source==source){
      removed++;
      continue;
    }

    const uint32_t offset=sizeof(HistoryHeader)+kept*sizeof(DailyHistoryRecord);
    if(!tmp.seek(offset,SeekSet)){
      src.close();tmp.close();
      LittleFS.remove(HISTORY_FILTER_TMP_FILE);
      return false;
    }

    if(tmp.write(reinterpret_cast<const uint8_t*>(&r),sizeof(r))!=sizeof(r)){
      src.close();tmp.close();
      LittleFS.remove(HISTORY_FILTER_TMP_FILE);
      return false;
    }

    kept++;
    if((li&0x7F)==0)yield();
  }

  newHeader.count=kept;
  newHeader.writeIndex=kept%newHeader.capacity;
  newHeader.crc=historyHeaderCrc(newHeader);

  if(!tmp.seek(0,SeekSet) ||
     tmp.write(reinterpret_cast<const uint8_t*>(&newHeader),sizeof(newHeader))!=sizeof(newHeader)){
    src.close();tmp.close();
    LittleFS.remove(HISTORY_FILTER_TMP_FILE);
    return false;
  }

  tmp.flush();
  src.close();
  tmp.close();

  LittleFS.remove(HISTORY_FILTER_BAK_FILE);

  if(!LittleFS.rename(HISTORY_FILE,HISTORY_FILTER_BAK_FILE)){
    LittleFS.remove(HISTORY_FILTER_TMP_FILE);
    return false;
  }

  if(!LittleFS.rename(HISTORY_FILTER_TMP_FILE,HISTORY_FILE)){
    LittleFS.rename(HISTORY_FILTER_BAK_FILE,HISTORY_FILE);
    LittleFS.remove(HISTORY_FILTER_TMP_FILE);
    return false;
  }

  LittleFS.remove(HISTORY_FILTER_BAK_FILE);

  historyHeader=newHeader;
  historyCurrentValid=false;
  historyInvalidateStatsCache();

  return true;
}

bool historyCompactAdjacentDuplicates(uint32_t& removed,uint32_t& invalid){
  removed=0;
  invalid=0;
  historyCompactPerformed=false;

  if(!historyReady)return false;
  if(historyHeader.count==0)return true;

  Serial.print(F("[HISTORY COMPACT] Start count="));
  Serial.println(historyHeader.count);

  File src=LittleFS.open(HISTORY_FILE,"r");
  if(!src){
    Serial.println(F(LTXT_LOG_COMPACT_UNREADABLE));
    return false;
  }

  LittleFS.remove(HISTORY_COMPACT_TMP_FILE);
  File tmp=LittleFS.open(HISTORY_COMPACT_TMP_FILE,"w+");
  if(!tmp){
    src.close();
    Serial.println(F(LTXT_LOG_COMPACT_TEMP));
    return false;
  }

  HistoryHeader newHeader=historyHeader;
  newHeader.count=0;
  newHeader.writeIndex=0;
  newHeader.crc=historyHeaderCrc(newHeader);

  if(tmp.write(reinterpret_cast<const uint8_t*>(&newHeader),sizeof(newHeader))!=sizeof(newHeader)){
    src.close();tmp.close();
    LittleFS.remove(HISTORY_COMPACT_TMP_FILE);
    return false;
  }

  const uint32_t oldest=historyOldestPhysicalIndex();
  DailyHistoryRecord pending{};
  bool havePending=false;
  uint32_t kept=0;

  auto flushPending=[&]()->bool{
    if(!havePending)return true;
    const uint32_t offset=sizeof(HistoryHeader)+kept*sizeof(DailyHistoryRecord);
    if(!tmp.seek(offset,SeekSet))return false;
    if(tmp.write(reinterpret_cast<const uint8_t*>(&pending),sizeof(pending))!=sizeof(pending))return false;
    kept++;
    havePending=false;
    return true;
  };

  for(uint32_t li=0;li<historyHeader.count;li++){
    const uint32_t physical=(oldest+li)%historyHeader.capacity;
    DailyHistoryRecord r;

    if(!historyReadRecordFromOpenFile(src,physical,r)){
      invalid++;
      if((li&0x1F)==0)yield();
      continue;
    }

    if(!havePending){
      pending=r;
      havePending=true;
    }else if(r.dayKey==pending.dayKey){
      // The last record of the same day wins.
      pending=r;
      removed++;
    }else{
      if(!flushPending()){
        src.close();tmp.close();
        LittleFS.remove(HISTORY_COMPACT_TMP_FILE);
        return false;
      }
      pending=r;
      havePending=true;
    }

    if((li&0x1F)==0)yield();
  }

  if(!flushPending()){
    src.close();tmp.close();
    LittleFS.remove(HISTORY_COMPACT_TMP_FILE);
    return false;
  }

  newHeader.count=kept;
  newHeader.writeIndex=kept%newHeader.capacity;
  newHeader.crc=historyHeaderCrc(newHeader);

  if(!tmp.seek(0,SeekSet) ||
     tmp.write(reinterpret_cast<const uint8_t*>(&newHeader),sizeof(newHeader))!=sizeof(newHeader)){
    src.close();tmp.close();
    LittleFS.remove(HISTORY_COMPACT_TMP_FILE);
    return false;
  }

  tmp.flush();
  src.close();
  tmp.close();
  yield();

  if(removed==0 && invalid==0){
    LittleFS.remove(HISTORY_COMPACT_TMP_FILE);
    Serial.println(F("[HISTORY COMPACT] Keine direkten Duplikate gefunden"));
    return true;
  }

  LittleFS.remove(HISTORY_COMPACT_BAK_FILE);

  if(!LittleFS.rename(HISTORY_FILE,HISTORY_COMPACT_BAK_FILE)){
    LittleFS.remove(HISTORY_COMPACT_TMP_FILE);
    return false;
  }

  if(!LittleFS.rename(HISTORY_COMPACT_TMP_FILE,HISTORY_FILE)){
    LittleFS.rename(HISTORY_COMPACT_BAK_FILE,HISTORY_FILE);
    LittleFS.remove(HISTORY_COMPACT_TMP_FILE);
    return false;
  }

  LittleFS.remove(HISTORY_COMPACT_BAK_FILE);

  historyHeader=newHeader;
  historyCurrentValid=false;
  historyInvalidateStatsCache();

  historyCompactDuplicates=removed;
  historyCompactInvalid=invalid;
  historyCompactPerformed=true;

  Serial.print(F(LTXT_LOG_COMPACT_DONE));
  Serial.print(removed);
  Serial.print(F(" invalid="));
  Serial.print(invalid);
  Serial.print(F(" count="));
  Serial.println(historyHeader.count);

  return true;
}

HistoryDuplicateScanResult historyScanDuplicates(){
  HistoryDuplicateScanResult result{};
  result.ok=false;

  if(!historyReady)return result;

  File f=LittleFS.open(HISTORY_FILE,"r");
  if(!f)return result;

  result.total=historyHeader.count;

  uint32_t lastDay=0;
  bool haveLastDay=false;
  int32_t previousOrdinal=INT32_MIN;

  for(uint32_t i=0;i<historyHeader.count;i++){
    DailyHistoryRecord rec;

    if(!historyReadChronologicalFromOpenFile(f,i,rec)){
      result.invalid++;
      if((i&0x7F)==0)yield();
      continue;
    }

    result.valid++;

    const int32_t ord=historyDayOrdinal(rec.dayKey);
    if(ord>=0){
      if(previousOrdinal!=INT32_MIN && ord<previousOrdinal)result.outOfOrder++;
      previousOrdinal=ord;
    }

    if(haveLastDay && rec.dayKey==lastDay){
      result.duplicates++;
    }else{
      lastDay=rec.dayKey;
      haveLastDay=true;
      result.uniqueDays++;
    }

    if((i&0x7F)==0)yield();
  }

  f.close();
  result.ok=true;

  Serial.print(F("[HISTORY DUPSCAN] total="));
  Serial.print(result.total);
  Serial.print(F(" valid="));
  Serial.print(result.valid);
  Serial.print(F(" unique="));
  Serial.print(result.uniqueDays);
  Serial.print(F(" duplicates="));
  Serial.print(result.duplicates);
  Serial.print(F(" invalid="));
  Serial.print(result.invalid);
  Serial.print(F(" outOfOrder="));
  Serial.println(result.outOfOrder);

  return result;
}

void handleHistoryMaintenancePage(){
  const uint32_t scanStartMs=millis();
  const HistoryDuplicateScanResult dupScan=historyScanDuplicates();
  const uint32_t scanTimeMs=millis()-scanStartMs;
  const uint32_t measuredRecords=historyCountSource(HISTORY_MEASURED);
  const uint32_t importedRecords=historyCountSource(HISTORY_IMPORTED);
  const uint32_t testRecords=historyCountSource(HISTORY_TEST);

  webStreamBegin(F(LTXT_HISTORY_MAINTENANCE));
  webStreamNav(1);

  server.sendContent(F(
    LHTML_MAINT_TITLE
    "<div class='links'><a class='btn' href='/history'>Zur Historie</a></div></div>"
    LHTML_MAINT_DESC
    "<div class='grid'>"
  ));

  webMetricCard(F(LTXT_CURRENT_RECORDS),String(historyHeader.count));
  webMetricCard(F(LTXT_SCAN_VALID),dupScan.ok?String(dupScan.valid):String(F(LTXT_ERROR)));
  webMetricCard(F(LTXT_UNIQUE_DAYS),dupScan.ok?String(dupScan.uniqueDays):String(F(LTXT_ERROR)));
  webMetricCard(F("Scan: Duplikate"),dupScan.ok?String(dupScan.duplicates):String(F(LTXT_ERROR)));
  webMetricCard(F(LTXT_SCAN_INVALID),dupScan.ok?String(dupScan.invalid):String(F(LTXT_ERROR)));
  webMetricCard(F(LTXT_SCAN_ORDER_ERRORS),dupScan.ok?String(dupScan.outOfOrder):String(F(LTXT_ERROR)));
  webMetricCard(F("Scan-Dauer"),String(scanTimeMs)+F(" ms"));
  webMetricCard(F("Duplikate erkannt"),String(historyRepairDuplicates));
  webMetricCard(F(LTXT_INVALID_CRC),String(historyRepairInvalid));
  webMetricCard(F(LTXT_ORDER_ERRORS),String(historyRepairOutOfOrder));
  webMetricCard(F("Entfernt"),String(historyRepairRemoved));
  webMetricCard(F(LTXT_LAST_REPAIR),
    historyRepairPerformed?String(F("JA")):String(F("NEIN")));
  webMetricCard(F(LTXT_SOURCE_MEASURED),String(measuredRecords)+F(" Records"));
  webMetricCard(F(LTXT_SOURCE_IMPORT),String(importedRecords)+F(" Records"));
  webMetricCard(F(LTXT_SOURCE_TEST),String(testRecords)+F(" Records"));
  webMetricCard(F("Quick-Compact entfernt"),String(historyCompactDuplicates));
  webMetricCard(F("Quick-Compact invalid"),String(historyCompactInvalid));

  server.sendContent(F(
    "</div>"
  ));

  if(!dupScan.ok){
    server.sendContent(F("<p class='muted'>Duplicate-Scan konnte nicht ausgeführt werden.</p>"));
  }else if(dupScan.duplicates>0 || dupScan.invalid>0 || dupScan.outOfOrder>0){
    server.sendContent(F("<p style='color:#ffb52e'><b>Bereinigung empfohlen:</b> "));
    webSendSafe(String(dupScan.duplicates));
    server.sendContent(F(" Duplikate, "));
    webSendSafe(String(dupScan.invalid));
    server.sendContent(F(LTXT_MAINT_INVALID_FRAGMENT));
    webSendSafe(String(dupScan.outOfOrder));
    server.sendContent(F(LTXT_MAINT_ORDER_FRAGMENT));
  }else{
    server.sendContent(F(LHTML_MAINT_CLEAN));
  }

  server.sendContent(F(
    "<form method='POST' action='/history/maintenance/compact' style='margin-top:16px'>"
    LHTML_CLEANUP_RUNNING_BUTTON
    "Schnelle Duplikatbereinigung</button></form>"
    "<form method='POST' action='/history/maintenance/repair' style='margin-top:16px' "
    LHTML_NORMALIZE_CONFIRM
    LHTML_NORMALIZE_RUNNING
    "Chronologie normalisieren / reparieren</button></form>"
    "<form method='POST' action='/history/maintenance/delete-test' style='margin-top:10px'>"
    "<button class='danger' type='submit' "
    LHTML_DELETE_TEST_CONFIRM
    LHTML_DELETE_TEST_BUTTON_END
    "<form method='POST' action='/history/maintenance/delete-imported' style='margin-top:10px'>"
    "<button class='danger' type='submit' "
    LHTML_DELETE_IMPORT_CONFIRM
    "Nur Importdaten löschen</button></form>"
    "<p class='muted' style='margin-top:12px'>"
    "<b>Chronologie normalisieren</b> ist besonders nach dem Import älterer Daten sinnvoll, wenn bereits neuere Messwerte vorhanden waren. "
    LTXT_MAINT_SORT_NOTE_1
    LTXT_MAINT_SORT_NOTE_2
    LTXT_MAINT_SORT_NOTE_3
    "</p></div>"
  ));

  webStreamEnd();
}

void handleHistoryCompactDuplicates(){
  const uint32_t before=historyHeader.count;
  const uint32_t heapBefore=ESP.getFreeHeap();
  const uint32_t startMs=millis();

  uint32_t removed=0,invalid=0;
  const bool ok=historyCompactAdjacentDuplicates(removed,invalid);

  const uint32_t elapsed=millis()-startMs;
  const uint32_t heapAfter=ESP.getFreeHeap();

  Serial.print(F("[HISTORY COMPACT] Ergebnis="));
  Serial.print(ok?F("OK"):F(LTXT_ERROR));
  Serial.print(F(" time="));
  Serial.print(elapsed);
  Serial.print(F(" ms heap="));
  Serial.print(heapBefore);
  Serial.print(F("->"));
  Serial.println(heapAfter);

  webStreamBegin(F(LTXT_HISTORY_MAINTENANCE));
  webStreamNav(1);

  server.sendContent(F(
    "<div class='card'><h1>Schnelle Duplikatbereinigung</h1><div class='grid'>"
  ));

  webMetricCard(F("Ergebnis"),ok?String(F("OK")):String(F(LTXT_ERROR)));
  webMetricCard(F("Vorher"),String(before)+F(" Records"));
  webMetricCard(F("Nachher"),String(historyHeader.count)+F(" Records"));
  webMetricCard(F("Duplikate entfernt"),String(removed));
  webMetricCard(F(LTXT_INVALID_REMOVED),String(invalid));
  webMetricCard(F("Dauer"),String(elapsed)+F(" ms"));

  server.sendContent(F(
    LHTML_QUICK_CLEAN_START
    LTXT_QUICK_CLEAN_NOTE_2
    "<div class='links' style='margin-top:16px'>"
    "<a class='btn' href='/history/maintenance'>" LTXT_MAINTENANCE "</a>"
    "<a class='btn' href='/history'>Historie</a>"
    "</div></div>"
  ));

  webStreamEnd();
}

void handleHistoryMaintenanceRepair(){
  const uint32_t heapBefore=ESP.getFreeHeap();
  const uint32_t startMs=millis();

  Serial.println(F(LTXT_LOG_MAINT_MANUAL));

  const bool ok=historyIntegrityCheckAndRepair();

  const uint32_t elapsed=millis()-startMs;
  const uint32_t heapAfter=ESP.getFreeHeap();

  Serial.print(F("[HISTORY MAINT] Ergebnis="));
  Serial.print(ok?F("OK"):F(LTXT_ERROR));
  Serial.print(F(" time="));
  Serial.print(elapsed);
  Serial.print(F(" ms heap="));
  Serial.print(heapBefore);
  Serial.print(F("->"));
  Serial.println(heapAfter);

  webStreamBegin(F(LTXT_HISTORY_MAINTENANCE));
  webStreamNav(1);

  server.sendContent(F(
    "<div class='card'><h1>History-Normalisierung abgeschlossen</h1>"
    "<div class='grid'>"
  ));

  webMetricCard(F("Ergebnis"),ok?String(F("OK")):String(F(LTXT_ERROR)));
  webMetricCard(F("Records"),String(historyHeader.count));
  webMetricCard(F("Duplikate"),String(historyRepairDuplicates));
  webMetricCard(F(LTXT_INVALID_CRC),String(historyRepairInvalid));
  webMetricCard(F(LTXT_ORDER_ERRORS),String(historyRepairOutOfOrder));
  webMetricCard(F("Entfernt"),String(historyRepairRemoved));
  webMetricCard(F("Repariert"),historyRepairPerformed?String(F("JA")):String(F("NEIN")));
  webMetricCard(F("Dauer"),String(elapsed)+F(" ms"));

  server.sendContent(F(
    "</div><div class='links' style='margin-top:16px'>"
    "<a class='btn' href='/history/maintenance'>" LTXT_MAINTENANCE "</a>"
    "<a class='btn' href='/history'>Historie</a>"
    "</div></div>"
  ));

  webStreamEnd();
}

void handleHistoryDeleteTestData(){
  const uint32_t before=historyHeader.count;
  uint32_t removed=0;

  Serial.println(F(LTXT_LOG_MAINT_TEST_DELETE));

  const bool ok=historyDeleteSource(HISTORY_TEST,removed);

  Serial.print(F(LTXT_LOG_MAINT_TEST_RESULT));
  Serial.print(ok?F("OK"):F(LTXT_ERROR));
  Serial.print(F(" entfernt="));
  Serial.print(removed);
  Serial.print(F(" count="));
  Serial.print(before);
  Serial.print(F("->"));
  Serial.println(historyHeader.count);

  webStreamBegin(F(LTXT_HISTORY_MAINTENANCE));
  webStreamNav(1);

  server.sendContent(F(
    LHTML_DELETE_TEST_TITLE
  ));

  webMetricCard(F("Ergebnis"),ok?String(F("OK")):String(F(LTXT_ERROR)));
  webMetricCard(F("Entfernt"),String(removed));
  webMetricCard(F("Vorher"),String(before)+F(" Records"));
  webMetricCard(F("Nachher"),String(historyHeader.count)+F(" Records"));

  server.sendContent(F(
    "</div><div class='links' style='margin-top:16px'>"
    "<a class='btn' href='/history/maintenance'>" LTXT_MAINTENANCE "</a>"
    "<a class='btn' href='/history'>Historie</a>"
    "</div></div>"
  ));

  webStreamEnd();
}

void handleHistoryDeleteImportedData(){
  const uint32_t before=historyHeader.count;
  uint32_t removed=0;

  Serial.println(F(LTXT_LOG_MAINT_IMPORT_DELETE));

  const bool ok=historyDeleteSource(HISTORY_IMPORTED,removed);

  Serial.print(F(LTXT_LOG_MAINT_IMPORT_RESULT));
  Serial.print(ok?F("OK"):F(LTXT_ERROR));
  Serial.print(F(" entfernt="));
  Serial.print(removed);
  Serial.print(F(" count="));
  Serial.print(before);
  Serial.print(F("->"));
  Serial.println(historyHeader.count);

  webStreamBegin(F(LTXT_HISTORY_MAINTENANCE));
  webStreamNav(1);

  server.sendContent(F(
    "<div class='card'><h1>Importdaten löschen</h1><div class='grid'>"
  ));

  webMetricCard(F("Ergebnis"),ok?String(F("OK")):String(F(LTXT_ERROR)));
  webMetricCard(F("Entfernt"),String(removed));
  webMetricCard(F("Vorher"),String(before)+F(" Records"));
  webMetricCard(F("Nachher"),String(historyHeader.count)+F(" Records"));

  server.sendContent(F(
    "</div><div class='links' style='margin-top:16px'>"
    "<a class='btn' href='/history/maintenance'>" LTXT_MAINTENANCE "</a>"
    "<a class='btn' href='/history'>Historie</a>"
    "</div></div>"
  ));

  webStreamEnd();
}


void handleHistoryPage(){
  const uint32_t historyPageHeapBefore=ESP.getFreeHeap();
  Serial.print(F("[WEB HISTORY PAGE] start heap="));
  Serial.print(historyPageHeapBefore);
  Serial.print(F(" maxBlock="));
  Serial.println(ESP.getMaxFreeBlockSize());

  webStreamBegin(F(LTXT_HISTORY));
  webStreamNav(1);

  server.sendContent(F(
    "<style>.periods{display:flex;gap:6px;flex-wrap:wrap}.periodBtn{background:#292929;border:1px solid #555;border-radius:999px;padding:7px 11px;color:#eee}.periodBtn.active,.monthYearsBtn.active{background:#1769aa}.monthYearsBtn{background:#292929;border:1px solid #555;border-radius:999px;padding:7px 11px;color:#eee}.chartToggles{display:flex;gap:10px;align-items:center;flex-wrap:wrap;font-size:.82rem;color:#bbb}.chartToggles label{display:flex;align-items:center;gap:4px}.chartToggles input{width:auto;margin:0}"
    ".chartWrap{height:300px;position:relative}.chart{width:100%;height:100%}.chartTip{position:absolute;display:none;pointer-events:none;min-width:170px;background:#101418;border:1px solid #4d5965;border-radius:9px;padding:8px;box-shadow:0 4px 14px #000;font-size:12px;z-index:5}.legend{display:flex;gap:14px;flex-wrap:wrap;color:#aaa;font-size:.82rem;margin-top:8px}.legend i{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:4px}@media(max-width:700px){.chartWrap{height:240px}}.chartToggles{margin-left:auto;padding:2px 0}.chartToggles label{padding:5px 8px;border:1px solid #3b3b3b;border-radius:999px;background:#202020;cursor:pointer}.chartToggles label.off{opacity:.38;cursor:not-allowed}.chartToggles input:disabled{cursor:not-allowed}.periods{display:flex;gap:6px;flex-wrap:wrap}.periodBtn,.monthYearsBtn{min-height:32px}.muted.compact{margin:5px 0 8px}</style>"
  ));
  server.sendContent(F(
    "<div class='card'><div class='topbar'><h1>" LTXT_HISTORY "</h1><div class='periods'>"
    "<button class='periodBtn' data-d='183'>" LTXT_HALF_YEAR "</button><button class='periodBtn active' data-d='365'>" LTXT_ONE_YEAR "</button>"
    "<button class='periodBtn' data-d='1825'>" LTXT_FIVE_YEARS "</button><button class='periodBtn' data-d='3650'>" LTXT_TEN_YEARS "</button></div>"
    "<div class='chartToggles'><label><input id='histShowTemp' type='checkbox' checked>" LTXT_TEMPERATURE "</label><label><input id='histShowHum' type='checkbox' checked>" LTXT_HUMIDITY_SHORT "</label></div></div>"
    "<p id='historyLoadStatus' class='muted compact'>" LTXT_HISTORY_LOADING "</p><div class='chartWrap'><canvas id='hc' class='chart'></canvas><div id='histTip' class='chartTip'></div></div>"
    "<div class='legend'><span><i style='background:#4da6ff'></i>" LTXT_TANK_LEVEL "</span><span><i style='background:#ffb52e'></i>" LTXT_CONSUMPTION "</span><span><i style='background:#42d65b'></i>" LTXT_REFILL "</span><span><i style='background:#ff8a65'></i>" LTXT_TEMPERATURE "</span><span><i style='background:#26c6da'></i>" LTXT_HUMIDITY "</span><span><i style='background:#ffd166'></i>" LTXT_IMPORT "</span><span><i style='background:#ff6b6b'></i>" LTXT_TEST_DATA "</span></div></div>"
    "<div class='card'><div class='topbar'><h2>" LTXT_MONTHLY_COMPARISON "</h2><div class='periods'>"
    "<button class='monthYearsBtn' data-y='3'>" LTXT_THREE_YEARS "</button><button class='monthYearsBtn active' data-y='5'>" LTXT_FIVE_YEARS "</button><button class='monthYearsBtn' data-y='10'>" LTXT_TEN_YEARS "</button></div></div>"
    "<p id='monthlyStatus' class='muted compact'>" LTXT_MONTHLY_LOADING "</p>"
    "<div class='chartWrap'><canvas id='mc' class='chart'></canvas><div id='monthTip' class='chartTip'></div></div><div id='monthlyLegend' class='legend'></div>"
    "<div style='overflow-x:auto'><table id='monthlyTable'></table></div></div>"
    "<div class='card'><h2>" LTXT_STATISTICS "</h2><div class='grid'><div class='metric'>" LTXT_PERIOD "<b id='sd'>--</b></div><div class='metric'>" LTXT_CONSUMPTION "<b id='sc'>-- L</b></div>"
    "<div class='metric'>" LTXT_REFILLS "<b id='sr'>-- L</b></div><div class='metric'>" LTXT_TANK_LEVEL "<b id='sl'>--</b></div>"
    "<div class='metric'>" LTXT_OLDEST_DAY "<b id='so'>--</b></div><div class='metric'>" LTXT_NEWEST_DAY "<b id='sn'>--</b></div>"
  ));
  server.sendContent(F(
    "<div class='metric'>" LTXT_DATASET "<b id='sy'>--</b></div></div></div>"
    "<div class='card'><h2>" LTXT_LATEST_REFILLS LHTML_RECENT_REFILLS_LOADING
  ));
  server.sendContent(F(
    "<div class='card'><h2>" LTXT_DATA "</h2><div class='links'><a class='btn' href='/history/import'>" LTXT_CSV_IMPORT_ACTION "</a><a class='btn' href='/history.csv?days=3650'>" LTXT_CSV_EXPORT_ACTION "</a><a class='btn' href='/history/maintenance'>" LTXT_MAINTENANCE "</a></div></div>"
    "<div class='card'><h2>" LTXT_TEST_DATA "</h2><div class='links'><form method='POST' action='/generate-test-history'><button type='submit'>" LTXT_ONE_YEAR_TEST "</button></form>"
    "<form method='POST' action='/generate-test-history-10y'><button type='submit'>" LTXT_TEN_YEARS_TEST "</button></form>"
    "<form method='POST' action='/clear-history' "
    "onsubmit=\"return confirm('ACHTUNG: Wirklich die komplette Historie unwiderruflich löschen?');\">"
    "<p style='color:#ff6b6b'><b>ACHTUNG:</b> Löscht die komplette History dauerhaft.</p>"
    LHTML_DELETE_CONFIRM_LABEL
  ));
  server.sendContent(F(
    LHTML_DELETE_CONFIRM_INPUT
    "<button class='danger' type='submit'>" LTXT_HISTORY_DELETE "</button></form></div></div>"
  ));

  server.sendContent(R"JS(
<script>
(function(){
const $=i=>document.getElementById(i),f=v=>Number.isFinite(Number(v))?Number(v).toFixed(1):'--',T=window.I18N||{};
let d=365,items=[],climateItems=[],histGeom=null,showTemp=true,showHum=true;
function updateHistoryClimateToggles(){
  const has=climateItems.length>0;
  const t=$('histShowTemp'),h=$('histShowHum');
  if(t){t.disabled=!has;t.closest('label')?.classList.toggle('off',!has)}
  if(h){h.disabled=!has;h.closest('label')?.classList.toggle('off',!has)}
}

const mn=['Jan','Feb','Mär','Apr','Mai','Jun','Jul','Aug','Sep','Okt','Nov','Dez'];
let monthYears=5,monthly=null,monthBars=[];

function dayKeyText(v){
  const n=Number(v)||0;
  if(n<10000101)return '--';
  const y=Math.floor(n/10000),m=Math.floor(n/100)%100,d=n%100;
  return String(d).padStart(2,'0')+'.'+String(m).padStart(2,'0')+'.'+y;
}
function dayKeyDate(v){
  const n=Number(v)||0;
  if(n<10000101)return null;
  const y=Math.floor(n/10000),m=Math.floor(n/100)%100,d=n%100;
  return new Date(y,m-1,d,12,0,0);
}
function dataSpanText(oldest,newest,count){
  const a=dayKeyDate(oldest),b=dayKeyDate(newest);
)JS");
  server.sendContent(R"JS(
  if(!a||!b||!count)return '--';
  const days=Math.max(1,Math.round((b-a)/86400000)+1);
  const years=days/365.2425;
  if(years>=1)return years.toFixed(1).replace('.',',')+' '+(T.years||'years')+' · '+count+' '+(T.days||'days');
  if(days>=30)return (days/30.44).toFixed(1).replace('.',',')+' '+(T.months||'months')+' · '+count+' '+(T.days||'days');
  return days+' '+(T.days||'days');
}

function draw(){
  const c=$('hc'),r=c.getBoundingClientRect(),w=Math.max(300,Math.floor(r.width)),h=Math.floor(r.height),z=devicePixelRatio||1;
  c.width=w*z;c.height=h*z;const x=c.getContext('2d');x.setTransform(z,0,0,z,0,0);x.clearRect(0,0,w,h);
  if(!items.length){x.fillStyle='#777';x.fillText(T.noData||'No data',20,30);return}
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
  const maxC=Math.max(1,...items.map(a=>Number(a.consumedLiters)||0));items.forEach(a=>{const q=px(a.time),bh=((Number(a.consumedLiters)||0)/maxC)*(ih*.30);if(bh>0){x.fillStyle='#ffb52e';x.fillRect(q-1,pt+ih-bh,2,bh)}});
  x.beginPath();items.forEach((a,i)=>{let q=px(a.time),y=py(a.percent);i?x.lineTo(q,y):x.moveTo(q,y)});x.strokeStyle='#4da6ff';x.lineWidth=2;x.stroke();
)JS");
  server.sendContent(R"JS(
  if(showHum&&climateItems.length){let begun=false;x.beginPath();climateItems.forEach(a=>{const v=Number(a.hAvg);if(!Number.isFinite(v))return;const q=px(a.time),y=py(v);begun?x.lineTo(q,y):x.moveTo(q,y);begun=true});if(begun){x.strokeStyle='#26c6da';x.lineWidth=1.8;x.stroke()}}
  if(showTemp&&climateItems.length){let begun=false;x.beginPath();climateItems.forEach(a=>{const v=Number(a.tAvg);if(!Number.isFinite(v))return;const q=px(a.time),y=pyT(v);begun?x.lineTo(q,y):x.moveTo(q,y);begun=true});if(begun){x.strokeStyle='#ff8a65';x.lineWidth=1.8;x.stroke()}}
)JS");
  server.sendContent(R"JS(
  items.forEach(a=>{const q=px(a.time),y=py(a.percent),src=Number(a.source)||0;if(src===1){x.strokeStyle='#ffd166';x.lineWidth=1.5;x.beginPath();x.arc(q,y,4,0,Math.PI*2);x.stroke()}else if(src===2){x.fillStyle='#ff6b6b';x.beginPath();x.moveTo(q,y-4);x.lineTo(q+4,y+4);x.lineTo(q-4,y+4);x.closePath();x.fill()}if(Number(a.refillLiters)>0){x.fillStyle='#42d65b';x.beginPath();x.arc(q,pt+ih-5,4,0,Math.PI*2);x.fill()}});
  histGeom={px:items.map(a=>px(a.time))}
}


const histC=$('hc'),histTip=$('histTip');

function nearestClimate(time){
  let best=null,bd=43200001;
  climateItems.forEach(v=>{
    const dd=Math.abs(Number(v.time)-Number(time));
    if(dd<bd){bd=dd;best=v}
  });
  return (best&&bd<=43200000)?best:null;
}

function histShowTip(e){
  if(!histGeom||!items.length)return;
)JS");
  server.sendContent(R"JS(
  const r=histC.getBoundingClientRect(),
        mx=(e.touches?e.touches[0].clientX:e.clientX)-r.left;

  let bi=-1,bd=99999;
  histGeom.px.forEach((q,i)=>{
    const dd=Math.abs(q-mx);
    if(dd<bd){bd=dd;bi=i}
  });

  if(bi<0||bd>28){
    histTip.style.display='none';
    return;
  }

  const a=items[bi],
        sn=Number(a.source)===1?(T.importText||'Import'):(Number(a.source)===2?(T.testData||'Test'):(T.measured||'Measured')),
        ci=nearestClimate(a.time);

  let extra='';
  if(ci){
    if(showTemp&&Number.isFinite(Number(ci.tAvg))){
      extra+='<br><span style="color:#ff8a65">'+(T.temperature||'Temperature')+': '+f(ci.tAvg)+' °C</span>';
    }
    if(showHum&&Number.isFinite(Number(ci.hAvg))){
      extra+='<br><span style="color:#26c6da">'+(T.humidity||'Humidity')+': '+f(ci.hAvg)+' %</span>';
    }
  }

  histTip.innerHTML=
)JS");
  server.sendContent(R"JS(
    '<b>'+new Date(a.time).toLocaleDateString(window.APP_LOCALE||'de-DE')+'</b>'+
    '<br>'+(T.tankLevel||'Level')+': '+f(a.percent)+' %'+
    '<br>'+(T.amount||'Amount')+': '+f(a.liters)+' L'+
    '<br>'+(T.consumption||'Consumption')+': '+f(a.consumedLiters)+' L'+
    '<br>'+(T.source||'Source')+': '+sn+
    (Number(a.refillLiters)>0
      ?'<br><span style="color:#65e572">'+(T.refill||'Refill')+': +'+f(a.refillLiters)+' L</span>'
      :'')+
    extra;

  histTip.style.display='block';
  histTip.style.left=Math.max(5,Math.min(histC.clientWidth-195,mx+10))+'px';
  histTip.style.top='8px';
}

histC.onmousemove=histShowTip;
histC.ontouchmove=histShowTip;
histC.onmouseleave=()=>histTip.style.display='none';
histC.ontouchend=()=>histTip.style.display='none';

async function loadClimate(n){
  climateItems=[];
  try{
    const r=await fetch('/api/history/climate?days='+n+'&x='+Date.now(),{
      cache:'no-store'
    });
)JS");
  server.sendContent(R"JS(
    const raw=await r.text();
    if(!r.ok)throw new Error('HTTP '+r.status);
    const j=JSON.parse(raw);
    climateItems=j.items||[];
  }catch(e){
    climateItems=[];
  }
  draw();
}

async function load(n){
  d=n;

  document.querySelectorAll('.periodBtn').forEach(b=>{
    b.classList.toggle('active',Number(b.dataset.d)===d);
  });

  const st=$('historyLoadStatus');
  st.textContent=T.loadingHistory||'Loading history …';
  st.style.color='#888';

  try{
    const r=await fetch('/api/history?days='+d+'&x='+Date.now(),{
      cache:'no-store'
    });
    const raw=await r.text();

    if(!r.ok)throw new Error('HTTP '+r.status);

    const j=JSON.parse(raw);
    if(j.ok===false)throw new Error(j.error||'API');

    items=j.items||[];
    const s=j.stats||{};

    $('sd').textContent=(s.days||0)+' '+(T.days||'days');
)JS");
  server.sendContent(R"JS(
    $('sc').textContent=f(s.consumptionLiters)+' L';
    $('sr').textContent=f(s.refillLiters)+' L';
    $('sl').textContent=
      (s.minPercent==null||s.maxPercent==null)
        ?'--'
        :f(s.minPercent)+'–'+f(s.maxPercent)+' %';

    $('so').textContent=dayKeyText(s.oldestDay);
    $('sn').textContent=dayKeyText(s.newestDay);
    $('sy').textContent=dataSpanText(
      s.oldestDay,
      s.newestDay,
      s.days||0
    );

    st.textContent=
      (T.loaded||'Loaded')+': '+items.length+' '+(T.points||'points')+
      ' / '+(s.days||0)+' '+(T.days||'days');
    st.style.color='#65e572';

    draw();
    await loadClimate(d);

    if(climateItems.length){st.textContent+=' · '+(T.climate||'Climate')+' '+climateItems.length+' '+(T.days||'days');}else{st.textContent+=' · '+(T.noClimateData||'No climate data');}
  }catch(e){
    items=[];
    climateItems=[];

    $('sd').textContent='--';
)JS");
  server.sendContent(R"JS(
    $('sc').textContent='-- L';
    $('sr').textContent='-- L';
    $('sl').textContent='--';
    $('so').textContent='--';
    $('sn').textContent='--';
    $('sy').textContent='--';

    st.textContent=(T.error||'Error')+': '+e.message;
    st.style.color='#ff6565';
    draw();
  }
}

function drawMonthly(){
  const c=$('mc');if(!c||!monthly)return;
  const r=c.getBoundingClientRect(),w=Math.max(320,Math.floor(r.width)),h=Math.floor(r.height),z=devicePixelRatio||1;
  c.width=w*z;c.height=h*z;
  const x=c.getContext('2d');x.setTransform(z,0,0,z,0,0);x.clearRect(0,0,w,h);
  const yrs=monthly.years||[],ms=monthly.months||[];let mx=1;
  ms.forEach(a=>(a||[]).forEach(v=>{if(v!=null&&Number(v)>mx)mx=Number(v)}));
  const pl=45,pr=10,pt=15,pb=35,iw=w-pl-pr,ih=h-pt-pb;
  x.strokeStyle='#333';
)JS");
  server.sendContent(R"JS(
  for(let g=0;g<=4;g++){let yy=pt+ih-g*ih/4;x.beginPath();x.moveTo(pl,yy);x.lineTo(w-pr,yy);x.stroke();x.fillStyle='#888';x.font='10px Arial';x.fillText(Math.round(mx*g/4)+'L',2,yy+3)}
  const group=iw/12,bw=Math.max(2,Math.min(12,(group-4)/Math.max(1,yrs.length)));monthBars=[];
  ms.forEach((a,m)=>{
    (a||[]).forEach((v,yi)=>{
      if(v==null)return;
      const val=Number(v)||0,q=pl+m*group+2+yi*bw,bh=val/mx*ih,hh=(yi*67)%360;
      x.fillStyle='hsl('+hh+' 65% 55%)';x.fillRect(q,pt+ih-bh,Math.max(1,bw-1),bh);
      monthBars.push({x:q,w:Math.max(1,bw-1),top:pt+ih-bh,bottom:pt+ih,month:m,year:yrs[yi],value:val});
    });
    x.fillStyle='#aaa';x.font='10px Arial';x.fillText(mn[m],pl+m*group+2,h-8)
  });
)JS");
  server.sendContent(R"JS(
  let lg='';yrs.forEach((y,yi)=>{lg+='<span><i style="background:hsl('+((yi*67)%360)+' 65% 55%)"></i>'+y+'</span>'});$('monthlyLegend').innerHTML=lg;
  let t='<tr><th>Monat</th>';yrs.forEach(y=>t+='<th>'+y+'</th>');t+='</tr>';
  ms.forEach((a,m)=>{t+='<tr><td>'+mn[m]+'</td>';yrs.forEach((y,yi)=>{let v=a?a[yi]:null;t+='<td>'+(v==null?'–':Math.round(v)+' L')+'</td>'});t+='</tr>'});
  $('monthlyTable').innerHTML=t
}

const monC=$('mc'),monTip=$('monthTip');
function monthShowTip(e){
  const r=monC.getBoundingClientRect(),mx=(e.touches?e.touches[0].clientX:e.clientX)-r.left,my=(e.touches?e.touches[0].clientY:e.clientY)-r.top;
  let b=monthBars.find(q=>mx>=q.x-2&&mx<=q.x+q.w+2&&my>=q.top-3&&my<=q.bottom+3);
  if(!b){monTip.style.display='none';return}
)JS");
  server.sendContent(R"JS(
  monTip.innerHTML='<b>'+mn[b.month]+' '+b.year+'</b><br>Verbrauch: '+Math.round(b.value)+' L';
  monTip.style.display='block';monTip.style.left=Math.max(5,Math.min(monC.clientWidth-175,mx+10))+'px';monTip.style.top='8px'
}
monC.onmousemove=monthShowTip;monC.ontouchmove=monthShowTip;monC.onmouseleave=()=>monTip.style.display='none';monC.ontouchend=()=>monTip.style.display='none';

async function loadMonthly(y){
  monthYears=y;document.querySelectorAll('.monthYearsBtn').forEach(b=>b.classList.toggle('active',Number(b.dataset.y)===monthYears));
  const st=$('monthlyStatus');st.textContent=T.loadingMonthly||'Loading monthly comparison …';
  try{
    let r=await fetch('/api/monthly-comparison?years='+monthYears+'&x='+Date.now(),{cache:'no-store'});
    let raw=await r.text();if(!r.ok)throw new Error('HTTP '+r.status);
)JS");
  server.sendContent(R"JS(
    let j=JSON.parse(raw);if(j.ok===false)throw new Error(j.error||'API');
    monthly=j;st.textContent='Vergleich '+(j.firstYear||'')+'–'+(j.currentYear||'');st.style.color='#65e572';drawMonthly()
  }catch(e){monthly=null;st.textContent=(T.error||'Error')+': '+e.message;st.style.color='#ff6565'}
}

async function loadRefills(){
  const b=$('recentRefills');
  try{
    let r=await fetch('/api/recent-refills?x='+Date.now(),{cache:'no-store'});
    if(!r.ok)throw new Error('HTTP '+r.status);
    let j=await r.json(),a=j.items||[];
    if(!a.length){b.innerHTML='<span class="muted">'+(T.noRefills||'No confirmed refills available.')+'</span>';return}
    let h='<table><tr><th>'+(T.date||'Date')+'</th><th>'+(T.amount||'Amount')+'</th><th>After</th></tr>';
)JS");
  server.sendContent(R"JS(
    a.forEach(v=>{h+='<tr><td>'+v.date+'</td><td>+'+f(v.liters)+' L</td><td>'+f(v.percent)+' %</td></tr>'});h+='</table>';b.innerHTML=h
  }catch(e){b.textContent=T.refillLoadError||'Refills could not be loaded.'}
}

document.querySelectorAll('.periodBtn').forEach(b=>b.onclick=()=>load(Number(b.dataset.d)));
$('histShowTemp').onchange=e=>{showTemp=!!e.target.checked;draw()};
$('histShowHum').onchange=e=>{showHum=!!e.target.checked;draw()};
document.querySelectorAll('.monthYearsBtn').forEach(b=>b.onclick=()=>loadMonthly(Number(b.dataset.y)));
updateHistoryClimateToggles();

async function initHistoryPage(){
  await load(365);
  await loadMonthly(5);
  await loadRefills();
}
initHistoryPage();
addEventListener('resize',()=>{draw();drawMonthly()});
})();
</script>
)JS");

  webStreamEnd();

  Serial.print(F("[WEB HISTORY PAGE] end heap="));
  Serial.print(ESP.getFreeHeap());
  Serial.print(F(" maxBlock="));
  Serial.print(ESP.getMaxFreeBlockSize());
  Serial.print(F(" delta="));
  Serial.println((int32_t)ESP.getFreeHeap()-(int32_t)historyPageHeapBefore);
}


// -----------------------------------------------------------------------------
// WIFI / AP
