#pragma once

#include "esp_err.h"

esp_err_t ulozHodnotu(const char *nazev, float hodnota);
esp_err_t nactiHodnotu(const char *nazev, float *hodnota);
