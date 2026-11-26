#include "vstupy.h"
#include "devicePins.h"

#include <cstdint>
#include "driver/gpio.h"
#include "esp_timer.h"

static constexpr int64_t DEBOUNCE_US = 50000;
static constexpr int64_t PODRZENI_US = 2000000;
static bool posledniStisk = false;
static bool stabilniStisk = false;
static bool resetProveden = false;
static int64_t casZmeny = 0;
static int64_t casStisku = 0;

esp_err_t vstupyInit() {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << DevicePins::RESET_MAX_NAKLONU;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  esp_err_t err = gpio_config(&cfg);
  if (err != ESP_OK) return err;
  posledniStisk = stabilniStisk = gpio_get_level(DevicePins::RESET_MAX_NAKLONU) == 0;
  casZmeny = casStisku = esp_timer_get_time();
  resetProveden = false;
  return ESP_OK;
}

StavResetu ctiReset() {
  int64_t ted = esp_timer_get_time();
  bool stisk = gpio_get_level(DevicePins::RESET_MAX_NAKLONU) == 0;
  if (stisk != posledniStisk) {
    posledniStisk = stisk;
    casZmeny = ted;
  }
  if (stisk != stabilniStisk && ted - casZmeny >= DEBOUNCE_US) {
    stabilniStisk = stisk;
    if (stisk) casStisku = ted;
    else resetProveden = false;
  }
  if (stisk && stabilniStisk && !resetProveden && ted - casStisku >= PODRZENI_US) {
    resetProveden = true;
    return {true, true, ted - casStisku};
  }
  return {stabilniStisk, false, stabilniStisk ? ted - casStisku : 0};
}
