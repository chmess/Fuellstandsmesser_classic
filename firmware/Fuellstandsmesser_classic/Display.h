#pragma once
#include <Arduino.h>

void lcdCursor(int16_t x, int16_t y);
void drawTankBar(int x, int y, int w, int h, float pct);
void drawDisplayPageMain();
void drawDisplayPageSensor();
void drawDisplayPageNetwork();
void drawDisplayPageClimate();
void drawDisplayPageSystem();
void drawDisplayContent();
void drawDisplay();

uint8_t displayNextAutoPage(uint8_t current);
