#include "Measurement.h"
#include "AppConstants.h"
#include "AppTypes.h"
#include "Sensors.h"
#include <Arduino.h>
#include <math.h>

extern Config cfg;
extern bool sensorOk;
extern uint8_t activeSensorType;
extern uint32_t lastTofRetryMs;
extern float rawDistanceMm;
extern float filteredDistanceMm;
extern float tankHeightNowMm;
extern float tankPercent;
extern float tankLiters;
extern uint32_t measurementCount;
extern uint32_t measurementErrors;
extern uint32_t sensorRecoveries;
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

const char* sensorName(uint8_t t);
void resetFilters();
void historyOnMeasurement();


// -----------------------------------------------------------------------------
float medianValue() {
  if (medUsed == 0) return NAN;

  float tmp[MEDIAN_COUNT];
  for (uint8_t i = 0; i < medUsed; i++) tmp[i] = medBuf[i];

  for (uint8_t i = 1; i < medUsed; i++) {
    float v = tmp[i];
    int8_t j = (int8_t)i - 1;
    while (j >= 0 && tmp[j] > v) {
      tmp[j + 1] = tmp[j];
      j--;
    }
    tmp[j + 1] = v;
  }

  return tmp[medUsed / 2];
}

float averageValue() {
  if (avgUsed == 0) return NAN;

  double sum = 0;
  for (uint8_t i = 0; i < avgUsed; i++) sum += avgBuf[i];

  return (float)(sum / avgUsed);
}

bool startupStabilizeDistance(float in,float& stableOut){
  stableOut=NAN;

  if(!isfinite(in))return false;

  if(in<cfg.minDistanceMm || in>cfg.maxDistanceMm){
    Serial.print(F("[STARTUP FILTER] ausser Bereich raw="));
    Serial.println(in,1);
    startupCandidateDistance=NAN;
    startupCandidateCount=0;
    return false;
  }

  if(startupMeasurementStable){
    stableOut=in;
    return true;
  }

  if(!isfinite(startupCandidateDistance) ||
     fabsf(in-startupCandidateDistance)>STARTUP_CONFIRM_TOLERANCE_MM){
    startupCandidateDistance=in;
    startupCandidateCount=1;
  }else{
    startupCandidateDistance=
      ((startupCandidateDistance*startupCandidateCount)+in)/
      (startupCandidateCount+1);
    if(startupCandidateCount<255)startupCandidateCount++;
  }

  Serial.print(F("[STARTUP FILTER] raw="));
  Serial.print(in,1);
  Serial.print(F(" mm confirm="));
  Serial.print(startupCandidateCount);
  Serial.print('/');
  Serial.println(STARTUP_CONFIRM_COUNT);

  if(startupCandidateCount<STARTUP_CONFIRM_COUNT)return false;

  startupMeasurementStable=true;
  stableOut=startupCandidateDistance;

  Serial.print(F(LTXT_LOG_STARTUP_STABLE));
  Serial.print(stableOut,1);
  Serial.println(F(" mm"));

  // Initialize the main filter cleanly with the confirmed startup value.
  for(uint8_t i=0;i<AVG_COUNT;i++)avgBuf[i]=0;
  for(uint8_t i=0;i<MEDIAN_COUNT;i++)medBuf[i]=0;
  avgPos=avgUsed=0;
  medPos=medUsed=0;
  lastAcceptedDistance=NAN;
  pendingJumpDistance=NAN;
  pendingJumpCount=0;

  return true;
}

bool filterDistance(float in, float& out) {
  if (!isfinite(in)) return false;

  if (in < cfg.minDistanceMm || in > cfg.maxDistanceMm) {
    Serial.print(F("[FILTER] ausser Bereich: raw="));
    Serial.print(in, 1);
    Serial.print(F(" mm erlaubt="));
    Serial.print(cfg.minDistanceMm);
    Serial.print('-');
    Serial.println(cfg.maxDistanceMm);
    return false;
  }

  // Large jump relative to the last accepted raw value:
  // accept only after several similar new measurements.
  if (isfinite(lastAcceptedDistance) &&
      fabsf(in - lastAcceptedDistance) > (float)cfg.maxJumpMm) {

    if (!isfinite(pendingJumpDistance) ||
        fabsf(in - pendingJumpDistance) > JUMP_CONFIRM_TOLERANCE_MM) {
      pendingJumpDistance = in;
      pendingJumpCount = 1;
    } else {
      // laufenden Kandidaten leicht mitteln
      pendingJumpDistance =
        ((pendingJumpDistance * pendingJumpCount) + in) / (pendingJumpCount + 1);
      if (pendingJumpCount < 255) pendingJumpCount++;
    }

    Serial.print(F("[FILTER] Sprung Kandidat raw="));
    Serial.print(in, 1);
    Serial.print(F(" mm delta="));
    Serial.print(fabsf(in - lastAcceptedDistance), 1);
    Serial.print(F(" mm confirm="));
    Serial.print(pendingJumpCount);
    Serial.print('/');
    Serial.println(JUMP_CONFIRM_COUNT);

    if (pendingJumpCount < JUMP_CONFIRM_COUNT) {
      return false;
    }

    Serial.print(F(LTXT_LOG_FILTER_NEW_LEVEL));
    Serial.print(pendingJumpDistance, 1);
    Serial.println(F(" mm"));

    // Discard the old filter contents so the new level takes effect immediately.
    for (uint8_t i = 0; i < AVG_COUNT; i++) avgBuf[i] = 0;
    for (uint8_t i = 0; i < MEDIAN_COUNT; i++) medBuf[i] = 0;
    avgPos = avgUsed = 0;
    medPos = medUsed = 0;

    in = pendingJumpDistance;
    pendingJumpDistance = NAN;
    pendingJumpCount = 0;
  } else {
    // normal value -> discard any pending jump candidate
    pendingJumpDistance = NAN;
    pendingJumpCount = 0;
  }

  lastAcceptedDistance = in;

  medBuf[medPos] = in;
  medPos = (medPos + 1) % MEDIAN_COUNT;
  if (medUsed < MEDIAN_COUNT) medUsed++;

  float med = medianValue();

  avgBuf[avgPos] = med;
  avgPos = (avgPos + 1) % AVG_COUNT;
  if (avgUsed < AVG_COUNT) avgUsed++;

  out = averageValue();
  return isfinite(out);
}

float tankCapacityLiters() {
  if (cfg.tankHeightMm <= 0) return NAN;

  double volumeM3 = 0;

  if (cfg.geometry == GEOMETRY_CYLINDER) {
    double rM = ((double)cfg.diameterMm / 2.0) / 1000.0;
    double hM = (double)cfg.tankHeightMm / 1000.0;
    if (rM <= 0 || hM <= 0) return NAN;
    volumeM3 = PI * rM * rM * hM;
  } else {
    double lM = (double)cfg.tankLengthMm / 1000.0;
    double wM = (double)cfg.tankWidthMm / 1000.0;
    double hM = (double)cfg.tankHeightMm / 1000.0;
    if (lM <= 0 || wM <= 0 || hM <= 0) return NAN;
    volumeM3 = lM * wM * hM;
  }

  return (float)(volumeM3 * 1000.0);
}

void calculateTank(float distanceMm) {
  tankHeightNowMm = NAN;
  tankPercent = NAN;
  tankLiters = NAN;

  if (!isfinite(distanceMm)) return;

  float span = cfg.emptyDistanceMm - cfg.fullDistanceMm;
  if (span <= 0 || cfg.tankHeightMm <= 0) return;

  float fraction = (cfg.emptyDistanceMm - distanceMm) / span;
  fraction = constrain(fraction, 0.0f, 1.0f);

  tankHeightNowMm = fraction * cfg.tankHeightMm;

  double volumeM3 = 0;

  if (cfg.geometry == GEOMETRY_CYLINDER) {
    double rM = ((double)cfg.diameterMm / 2.0) / 1000.0;
    double hM = (double)tankHeightNowMm / 1000.0;
    if (rM <= 0) return;
    volumeM3 = PI * rM * rM * hM;
  } else {
    double lM = (double)cfg.tankLengthMm / 1000.0;
    double wM = (double)cfg.tankWidthMm / 1000.0;
    double hM = (double)tankHeightNowMm / 1000.0;
    if (lM <= 0 || wM <= 0) return;
    volumeM3 = lM * wM * hM;
  }

  tankLiters = (float)(volumeM3 * 1000.0);

  float cap = tankCapacityLiters();
  if (isfinite(cap) && cap > 0) {
    tankPercent = constrain(tankLiters / cap * 100.0f, 0.0f, 100.0f);
  }
}

void performMeasurement() {
  uint16_t d = 0;

  if (!readToF(d)) {
    measurementErrors++;

    if (millis() - lastTofRetryMs >= TOF_RETRY_MS) {
      lastTofRetryMs = millis();
      Serial.println(F("[TOF] Recovery-Versuch"));
      if (initToF()) {
        sensorRecoveries++;
        resetFilters();
      }
    }

    return;
  }

  float corrected = (float)((int32_t)d + (int32_t)cfg.sensorOffsetMm);

  // Immediately after boot / sensor recovery, first require several similar raw values
  // to confirm the reading. This prevents a single incorrect startup value from contaminating history,
  // noch MQTT noch den Sprungfilter verunreinigen.
  float startupStable = NAN;
  if(!startupStabilizeDistance(corrected,startupStable)){
    return;
  }

  corrected=startupStable;

  float filtered = NAN;
  if (!filterDistance(corrected, filtered)) {
    measurementErrors++;
    return;
  }

  rawDistanceMm = corrected;
  filteredDistanceMm = filtered;
  calculateTank(filteredDistanceMm);
  measurementCount++;

  Serial.print(F("[MEAS] "));
  Serial.print(sensorName(activeSensorType));
  Serial.print(F(" raw="));
  Serial.print(rawDistanceMm, 1);
  Serial.print(F(" mm filtered="));
  Serial.print(filteredDistanceMm, 1);
  Serial.print(F(" mm H="));
  Serial.print(tankHeightNowMm, 1);
  Serial.print(F(" mm "));
  Serial.print(tankPercent, 1);
  Serial.print(F("% "));
  Serial.print(tankLiters, 1);
  Serial.println(F(" L"));

  historyOnMeasurement();
}

