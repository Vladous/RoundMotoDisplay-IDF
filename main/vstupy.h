#pragma once

#include <cstdint>
#include "esp_err.h"

struct StavResetu {
  bool stisknuto;
  bool reset;
  int64_t dobaDrzeni;
};

esp_err_t vstupyInit();
StavResetu ctiReset();
