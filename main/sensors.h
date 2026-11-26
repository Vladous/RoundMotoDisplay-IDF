#pragma once

#include "esp_err.h"

extern float Teplota;
extern float NaklonX;
extern float NaklonY;
extern float MaxNaklonKladny;
extern float MaxNaklonZaporny;

esp_err_t sensorsInit();
void updateTemp();
void updateNaklon();
void updateMaxNaklony();
bool resetMaxNaklony();
