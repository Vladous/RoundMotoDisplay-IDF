#pragma once

#include "esp_err.h"

esp_err_t displayInit();
void drawScale();
void updateScale(float naklon);
void updateMaxScale(float zaporny, float kladny);
void drawCelsius();
void drawTeplota(float teplota);
void drawBtStatus(bool online);
void drawResetScreen(int faze);
void drawMainScreen(float naklon, float zaporny, float kladny, float teplota, bool bt);
