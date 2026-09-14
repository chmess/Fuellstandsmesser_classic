#pragma once
#include <Arduino.h>
#include <FS.h>
#include "HistoryTypes.h"

// Public History API. Implementation remains in 60_History.ino for Stage 5A;
// next stage moves it to History.cpp after this interface compiles cleanly.

HistoryImportParsed parseHistoryImportLine(String line, uint32_t today, uint32_t oldest);
void historyDeriveImportFlow(HistoryImportParsed& p, uint32_t prevDay, uint16_t prevLiters, bool prevValid);
void historyApplyImportedClimate(const HistoryImportParsed& p, DailyHistoryRecord& r);
float historyConsumptionDays(uint16_t days);
void historyInvalidateStatsCache();
void historyBuildStatsCache();
int32_t historyDayOrdinal(uint32_t dayKey);
bool historyIntegrityCheckAndRepair();
bool historyResetFile();
bool historyAppendExternal(uint32_t dayKey,uint16_t liters,uint16_t cons,uint16_t refill,uint8_t source);
bool historyGenerateFast(uint16_t days);
void historyGenerate(uint16_t days);
bool parseImportDate(String v,uint32_t& dayKey);
bool historyImportIndexCreate(File& idxFile,uint32_t slots);
bool historyImportIndexWrite(File& idxFile,uint32_t slot,uint32_t physicalIndex);
bool historyImportIndexRead(File& idxFile,uint32_t slot,uint32_t& physicalIndex);
bool historyWriteRecordToOpenFile(File& f,uint32_t physicalIndex,DailyHistoryRecord& r);
bool historyCompactAdjacentDuplicates(uint32_t& removed,uint32_t& invalid);
void handleHistoryCompactDuplicates();
void historyOnMeasurement();
void historySetupAfterFilesystem();
void historySetupTime();
void historyLoop();
void handleHistoryPage();
void handleHistoryApi();
void handleClimateHistoryApi();
void handleMonthlyComparisonApi();
void handleHistoryCsv();
void handleRecentRefills();
void handleGenerateTestHistory();
void handleGenerate10YearTestHistory();
void handleClearHistory();
void handleHistoryImportPage();
void handleHistoryImportPreview();
void handleHistoryImportApply();
void handleHistoryImportCancel();
void handleHistoryImportUpload();
HistoryDuplicateScanResult historyScanDuplicates();
void handleHistoryMaintenancePage();
void handleHistoryMaintenanceRepair();
void handleHistoryDeleteTestData();
void handleHistoryDeleteImportedData();
uint32_t historyCountSource(uint8_t source);
bool historyDeleteSource(uint8_t source,uint32_t& removed);


// Shared History state / constants used by Web and CLI.
extern const char* HISTORY_FILE;
extern const char* HISTORY_IMPORT_PREVIEW_FILE;
extern const char* HISTORY_IMPORT_INDEX_FILE;
extern const char* HISTORY_REPAIR_INDEX_FILE;
extern const char* HISTORY_REPAIR_TMP_FILE;
extern const char* HISTORY_REPAIR_BAK_FILE;
extern const char* HISTORY_FILTER_TMP_FILE;
extern const char* HISTORY_FILTER_BAK_FILE;
extern const char* HISTORY_COMPACT_TMP_FILE;
extern const char* HISTORY_COMPACT_BAK_FILE;
extern const char* HISTORY_V2_MIGRATE_TMP_FILE;
extern const char* HISTORY_V2_MIGRATE_BAK_FILE;
extern const uint16_t HISTORY_REFILL_MIN_LITERS;
extern HistoryHeader historyHeader;
extern bool historyReady;
extern DailyHistoryRecord historyCurrent;
extern bool historyCurrentValid;
extern uint32_t historyWriteErrors;
extern uint32_t historyRepairDuplicates;
extern uint32_t historyRepairInvalid;
extern uint32_t historyRepairRemoved;
extern bool historyRepairPerformed;
extern uint32_t historyCompactDuplicates;
extern uint32_t historyCompactInvalid;
extern bool historyCompactPerformed;
extern HistoryStatsCache historyStatsCache;
extern uint32_t historyApiRequests;
extern uint32_t historyApiErrors;
extern uint32_t historyApiLastMs;
extern uint32_t historyApiLastItems;

bool historyDateNow(uint32_t& dayKey);
bool historyNewestRecordFromOpenFile(File& f, DailyHistoryRecord& r, uint32_t& logicalIndex);
uint32_t historyFirstDayForAnchor(uint32_t anchorDay, uint16_t days);
uint32_t historyOldestPhysicalIndex();
bool historyReadRecordFromOpenFile(File& f, uint32_t physicalIndex, DailyHistoryRecord& r);
void setupFilesystem();