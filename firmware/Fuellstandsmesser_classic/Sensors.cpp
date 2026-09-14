#include "Sensors.h"
#include "AppConstants.h"
#include "AppTypes.h"

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include <Adafruit_VL53L1X.h>

extern Config cfg;
extern Adafruit_VL53L0X vl53l0x;
extern Adafruit_VL53L1X vl53l1x;
extern bool sensorOk;
extern uint8_t activeSensorType;
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

bool i2cPresent(uint8_t addr);
bool readReg8(uint8_t addr, uint8_t reg, uint8_t& v);
bool readReg16(uint8_t addr, uint16_t reg, uint8_t& v);

bool aht10Present(){
  Wire.beginTransmission(AHT10_ADDR);
  return Wire.endTransmission()==0;
}

float calculateDewPointMagnus(float temperatureC,float humidityPercent){
  if(!isfinite(temperatureC) || !isfinite(humidityPercent) ||
     humidityPercent<=0.0f || humidityPercent>100.0f)return NAN;

  const float a=17.62f;
  const float b=243.12f;
  const float gamma=logf(humidityPercent/100.0f)+(a*temperatureC)/(b+temperatureC);
  return (b*gamma)/(a-gamma);
}

const __FlashStringHelper* ahtStatusText(){
  if(!cfg.ahtEnabled)return F("AUS");
  if(ahtInitialized && lastAhtValidMs==0)return F("WARTET");
  if(!ahtInitialized)return F("LOST");
  if(!ahtOk)return F("LOST");
  if((uint32_t)(millis()-lastAhtValidMs)>AHT_STALE_MS)return F("STALE");
  return F("OK");
}

bool initAht10(){
  if(!cfg.ahtEnabled){
    ahtInitialized=false;
    ahtOk=false;
    return false;
  }

  ahtProbeCount++;

  if(!aht10Present()){
    ahtInitialized=false;
    ahtOk=false;
    return false;
  }

  Serial.println(F("[AHT10] gefunden @ 0x38"));

  Wire.beginTransmission(AHT10_ADDR);
  Wire.write(0xBA);
  if(Wire.endTransmission()!=0){
    ahtInitialized=false;
    ahtOk=false;
    Serial.println(F("[AHT10] Soft-Reset FEHLER"));
    return false;
  }
  delay(25);
  yield();

  Wire.beginTransmission(AHT10_ADDR);
  Wire.write(0xE1);
  Wire.write(0x08);
  Wire.write(0x00);
  if(Wire.endTransmission()!=0){
    ahtInitialized=false;
    ahtOk=false;
    Serial.println(F("[AHT10] Init FEHLER"));
    return false;
  }

  delay(15);
  yield();

  ahtInitialized=true;
  ahtOk=false;
  ahtConsecutiveErrors=0;

  Serial.print(F("[AHT10] initialisiert | warte auf gueltige Messung | Offset T="));
  Serial.print(cfg.ahtTemperatureOffsetC,1);
  Serial.print(F(" C RH="));
  Serial.print(cfg.ahtHumidityOffsetPercent,1);
  Serial.print(F(" % | Intervall="));
  Serial.print(cfg.ahtIntervalMs/1000UL);
  Serial.println(F(" s"));
  return true;
}

bool readAht10(){
  if(!cfg.ahtEnabled)return false;

  if(!aht10Present()){
    const bool wasPresent=ahtInitialized || ahtOk;
    if(wasPresent)Serial.println(F("[AHT10] LOST: kein ACK @ 0x38"));
    ahtInitialized=false;
    ahtOk=false;
    ahtTemperatureC=NAN;
    ahtHumidityPercent=NAN;
    ahtDewPointC=NAN;
    ahtCondensationReserveC=NAN;
    if(ahtConsecutiveErrors<255)ahtConsecutiveErrors++;
    ahtErrorCount++;
    return false;
  }

  if(!ahtInitialized)return false;

  Wire.beginTransmission(AHT10_ADDR);
  Wire.write(0xAC);
  Wire.write(0x33);
  Wire.write(0x00);
  if(Wire.endTransmission()!=0){
    ahtOk=false;
    if(ahtConsecutiveErrors<255)ahtConsecutiveErrors++;
    ahtErrorCount++;
    Serial.println(F("[AHT10] Trigger FEHLER"));
    return false;
  }

  for(uint8_t i=0;i<9;i++){
    delay(10);
    yield();
  }

  const uint8_t requested=6;
  if(Wire.requestFrom((uint8_t)AHT10_ADDR,requested)!=(int)requested){
    ahtOk=false;
    if(ahtConsecutiveErrors<255)ahtConsecutiveErrors++;
    ahtErrorCount++;
    Serial.println(F("[AHT10] Read FEHLER: zu wenig Bytes"));
    return false;
  }

  uint8_t d[6];
  for(uint8_t i=0;i<6;i++)d[i]=Wire.read();

  if(d[0]&0x80){
    if(ahtConsecutiveErrors<255)ahtConsecutiveErrors++;
    ahtErrorCount++;
    Serial.println(F("[AHT10] Messung noch BUSY"));
    return false;
  }

  const uint32_t rawHum=((uint32_t)d[1]<<12)|((uint32_t)d[2]<<4)|((uint32_t)d[3]>>4);
  const uint32_t rawTemp=(((uint32_t)d[3]&0x0F)<<16)|((uint32_t)d[4]<<8)|(uint32_t)d[5];

  const float humidityRaw=((float)rawHum*100.0f)/1048576.0f;
  const float temperatureRaw=((float)rawTemp*200.0f)/1048576.0f-50.0f;

  float humidity=humidityRaw+cfg.ahtHumidityOffsetPercent;
  float temperature=temperatureRaw+cfg.ahtTemperatureOffsetC;
  if(humidity<0.0f)humidity=0.0f;
  if(humidity>100.0f)humidity=100.0f;

  if(!isfinite(temperature) || temperature<-40.0f || temperature>85.0f ||
     !isfinite(humidity) || humidity<0.0f || humidity>100.0f){
    ahtOk=false;
    if(ahtConsecutiveErrors<255)ahtConsecutiveErrors++;
    ahtErrorCount++;
    Serial.print(F("[AHT10] Plausibilitaet FEHLER T="));
    Serial.print(temperature,1);
    Serial.print(F(" H="));
    Serial.println(humidity,1);
    return false;
  }

  const bool hadValidValue=isfinite(ahtTemperatureC) && isfinite(ahtHumidityPercent);
  const bool recovering=!ahtOk || lastAhtValidMs==0;

  if(hadValidValue){
    temperature=ahtTemperatureC + AHT_SMOOTH_ALPHA*(temperature-ahtTemperatureC);
    humidity=ahtHumidityPercent + AHT_SMOOTH_ALPHA*(humidity-ahtHumidityPercent);
  }

  const float dew=calculateDewPointMagnus(temperature,humidity);
  if(!isfinite(dew)){
    ahtOk=false;
    if(ahtConsecutiveErrors<255)ahtConsecutiveErrors++;
    ahtErrorCount++;
    return false;
  }

  ahtTemperatureC=temperature;
  ahtHumidityPercent=humidity;
  ahtDewPointC=dew;
  ahtCondensationReserveC=temperature-dew;
  ahtOk=true;
  ahtInitialized=true;
  ahtReadCount++;
  ahtConsecutiveErrors=0;
  lastAhtValidMs=millis();

  if(recovering){
    ahtRecoveryCount++;
    lastAhtRecoveryMs=millis();
    Serial.print(F("[AHT10] RECOVERED #"));
    Serial.println(ahtRecoveryCount);
  }

  Serial.print(F("[AHT10] T="));
  Serial.print(ahtTemperatureC,1);
  Serial.print(F(" C RH="));
  Serial.print(ahtHumidityPercent,1);
  Serial.print(F(" % Taupunkt="));
  Serial.print(ahtDewPointC,1);
  Serial.print(F(" C Reserve="));
  Serial.print(ahtCondensationReserveC,1);
  Serial.print(F(" C smooth="));
  Serial.println(hadValidValue?F("JA"):F("START"));

  return true;
}

bool initL0X() {
  Serial.println(F("[TOF] init VL53L0X"));

  if (!vl53l0x.begin(
        VL53_ADDR,
        false,
        &Wire,
        Adafruit_VL53L0X::VL53L0X_SENSE_DEFAULT)) {
    Serial.println(F("[TOF] VL53L0X FEHLER"));
    return false;
  }

  activeSensorType = SENSOR_VL53L0X;
  Serial.println(F("[TOF] VL53L0X OK"));
  return true;
}

bool initL1X() {
  Serial.println(F("[TOF] init VL53L1X"));

  if (!vl53l1x.begin(VL53_ADDR, &Wire, false)) {
    Serial.println(F("[TOF] VL53L1X FEHLER"));
    return false;
  }

  vl53l1x.setTimingBudget(50);

  if (!vl53l1x.startRanging()) {
    Serial.println(F("[TOF] VL53L1X ranging FEHLER"));
    return false;
  }

  activeSensorType = SENSOR_VL53L1X;
  Serial.println(F("[TOF] VL53L1X OK"));
  return true;
}

bool initToF() {
  sensorOk = false;
  activeSensorType = SENSOR_AUTO;

  if (!i2cPresent(VL53_ADDR)) {
    Serial.println(F("[TOF] 0x29 nicht gefunden"));
    return false;
  }

  if (cfg.sensorType == SENSOR_VL53L0X) {
    sensorOk = initL0X();
    return sensorOk;
  }

  if (cfg.sensorType == SENSOR_VL53L1X) {
    sensorOk = initL1X();
    return sensorOk;
  }

  uint8_t id = 0;

  if (readReg8(VL53_ADDR, 0xC0, id) && id == 0xEE) {
    Serial.println(F("[TOF] erkannt: VL53L0X"));
    sensorOk = initL0X();
    return sensorOk;
  }

  if (readReg16(VL53_ADDR, 0x010F, id) && id == 0xEA) {
    Serial.println(F("[TOF] erkannt: VL53L1X"));
    sensorOk = initL1X();
    return sensorOk;
  }

  Serial.println(F("[TOF] ID unklar -> Treibertest"));

  if (initL0X()) {
    sensorOk = true;
    return true;
  }

  if (initL1X()) {
    sensorOk = true;
    return true;
  }

  return false;
}

bool readToF(uint16_t& mm) {
  if (!sensorOk) return false;

  if (activeSensorType == SENSOR_VL53L0X) {
    VL53L0X_RangingMeasurementData_t m;
    VL53L0X_Error e = vl53l0x.rangingTest(&m, false);

    if (e != 0 || m.RangeStatus != 0 || m.RangeMilliMeter == 0) {
      return false;
    }

    mm = m.RangeMilliMeter;
    return true;
  }

  if (activeSensorType == SENSOR_VL53L1X) {
    if (!vl53l1x.dataReady()) {
      return false;
    }

    int16_t d = vl53l1x.distance();
    uint8_t st = (uint8_t)vl53l1x.vl_status;
    vl53l1x.clearInterrupt();

    if (d <= 0 || st != 0) return false;

    mm = (uint16_t)d;
    return true;
  }

  return false;
}
