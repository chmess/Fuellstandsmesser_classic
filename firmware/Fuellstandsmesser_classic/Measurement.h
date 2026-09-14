#pragma once
#include <Arduino.h>

float medianValue();
float averageValue();
bool startupStabilizeDistance(float in, float& stableOut);
bool filterDistance(float in, float& out);
float tankCapacityLiters();
void calculateTank(float distanceMm);
void performMeasurement();
