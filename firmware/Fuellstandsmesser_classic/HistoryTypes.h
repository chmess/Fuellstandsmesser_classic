#pragma once
#include <Arduino.h>

enum HistorySource : uint8_t {
  HISTORY_MEASURED = 0,
  HISTORY_IMPORTED = 1,
  HISTORY_TEST = 2
};

struct __attribute__((packed)) HistoryHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t recordSize;
  uint32_t capacity;
  uint32_t count;
  uint32_t writeIndex;
  uint32_t crc;
};

struct __attribute__((packed)) DailyHistoryRecordV2 {
  uint32_t dayKey;
  uint16_t samples;
  uint16_t levelLiters;
  uint16_t avgPermille;
  uint16_t minPermille;
  uint16_t maxPermille;
  uint16_t firstLiters;
  uint16_t consumptionLiters;
  uint16_t refillLiters;
  uint8_t source;
  uint8_t flags;
  uint16_t crc16;
};

static_assert(sizeof(DailyHistoryRecordV2) == 24, "DailyHistoryRecordV2 muss 24 Byte haben");

struct __attribute__((packed)) DailyHistoryRecord {
  uint32_t dayKey;          // YYYYMMDD
  uint16_t samples;
  uint16_t levelLiters;     // Tages-Endstand
  uint16_t avgPermille;     // 0..1000 = 0.0..100.0 %
  uint16_t minPermille;
  uint16_t maxPermille;
  uint16_t firstLiters;
  uint16_t consumptionLiters;
  uint16_t refillLiters;
  uint8_t source;
  uint8_t flags;

  // History V3 Klima, kompakt:
  // Temperatur = 0.5 C Schritte, Code 0=-40.0 C, 250=85.0 C, 255=kein Wert.
  // Feuchte = volle Prozent 0..100, 255=kein Wert.
  uint8_t tempAvgHalfC;
  uint8_t tempMinHalfC;
  uint8_t tempMaxHalfC;
  uint8_t humidityAvgPct;
  uint8_t humidityMinPct;
  uint8_t humidityMaxPct;
  uint16_t climateSamples;

  uint16_t crc16;
};

static_assert(sizeof(DailyHistoryRecord) == 32, "DailyHistoryRecord muss 32 Byte haben");

struct HistoryDuplicateScanResult {
  bool ok;
  uint32_t total;
  uint32_t valid;
  uint32_t uniqueDays;
  uint32_t duplicates;
  uint32_t invalid;
  uint32_t outOfOrder;
};

struct HistoryImportParsed {
  bool valid;
  bool autoDerived;
  bool climateValid;
  uint32_t dayKey;
  uint16_t liters;
  uint16_t consumption;
  uint16_t refill;
  uint8_t source;
  float tempAvgC;
  float tempMinC;
  float tempMaxC;
  float humidityAvgPct;
  float humidityMinPct;
  float humidityMaxPct;
  uint16_t climateSamples;
  String reason;
};


struct HistoryStatsCache {
  bool valid;
  uint32_t builtMs;
  float c1;
  float c7;
  float c30;
  float c365;
};
