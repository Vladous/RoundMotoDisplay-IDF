#include "sensors.h"
#include "devicePins.h"
#include "uloziste.h"

#include <cmath>
#include <cstdint>
#include "driver/i2c_master.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

float Teplota = NAN;
float NaklonX = NAN;
float NaklonY = NAN;
float MaxNaklonKladny = 0;
float MaxNaklonZaporny = 0;

static const char *TAG = "sensors";
static adc_oneshot_unit_handle_t adc = nullptr;
static adc_cali_handle_t kalibrace = nullptr;
static i2c_master_bus_handle_t i2c = nullptr;
static i2c_master_dev_handle_t mpu[2] = {};
static int aktivniMpu = -1;
static int64_t dalsiPokusMpu = 0;
static bool chybaMpu = false;
static float posledniTeploty[10];
static int indexMereni = 0;
static int pocetMereni = 0;

struct MaximumNaklonu {
  const char *nazev;
  float *hodnota;
  bool zmeneno;
  int64_t dalsiPokus;
};
static MaximumNaklonu maxima[] = {
  {"naklon_plus", &MaxNaklonKladny, false, 0},
  {"naklon_minus", &MaxNaklonZaporny, false, 0},
};

esp_err_t sensorsInit() {
  for (auto &maximum : maxima) {
    esp_err_t err = nactiHodnotu(maximum.nazev, maximum.hodnota);
    if (err == ESP_ERR_NVS_NOT_FOUND) continue;
    if (err != ESP_OK) return err;
    if (!std::isfinite(*maximum.hodnota) ||
        (maximum.hodnota == &MaxNaklonKladny && *maximum.hodnota < 0) ||
        (maximum.hodnota == &MaxNaklonZaporny && *maximum.hodnota > 0)) {
      ESP_LOGE(TAG, "Neplatna ulozena hodnota %s", maximum.nazev);
      return ESP_ERR_INVALID_RESPONSE;
    }
  }

  adc_oneshot_unit_init_cfg_t adcCfg = {};
  adcCfg.unit_id = ADC_UNIT_1;
  esp_err_t err = adc_oneshot_new_unit(&adcCfg, &adc);
  if (err != ESP_OK) return err;

  adc_oneshot_chan_cfg_t kanal = {};
  kanal.atten = ADC_ATTEN_DB_12;
  kanal.bitwidth = ADC_BITWIDTH_12;
  err = adc_oneshot_config_channel(adc, DevicePins::THERMISTOR, &kanal);
  if (err != ESP_OK) return err;

  adc_cali_line_fitting_config_t calCfg = {};
  calCfg.unit_id = ADC_UNIT_1;
  calCfg.atten = ADC_ATTEN_DB_12;
  calCfg.bitwidth = ADC_BITWIDTH_12;
  calCfg.default_vref = 1100;
  err = adc_cali_create_scheme_line_fitting(&calCfg, &kalibrace);
  if (err != ESP_OK) return err;

  i2c_master_bus_config_t busCfg = {};
  busCfg.i2c_port = I2C_NUM_0;
  busCfg.sda_io_num = DevicePins::MPU_SDA;
  busCfg.scl_io_num = DevicePins::MPU_SCL;
  busCfg.clk_source = I2C_CLK_SRC_DEFAULT;
  busCfg.glitch_ignore_cnt = 7;
  busCfg.flags.enable_internal_pullup = true;
  err = i2c_new_master_bus(&busCfg, &i2c);
  if (err != ESP_OK) return err;

  for (int i = 0; i < 2; i++) {
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = 0x68 + i;
    cfg.scl_speed_hz = 100000;
    err = i2c_master_bus_add_device(i2c, &cfg, &mpu[i]);
    if (err != ESP_OK) return err;
  }
  return ESP_OK;
}

void updateTemp() {
  int soucet = 0;
  for (int i = 0; i < 16; i++) {
    int raw = 0;
    int mv = 0;
    esp_err_t err = adc_oneshot_read(adc, DevicePins::THERMISTOR, &raw);
    if (err == ESP_OK) err = adc_cali_raw_to_voltage(kalibrace, raw, &mv);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Chyba mereni termistoru: %s", esp_err_to_name(err));
      Teplota = NAN;
      return;
    }
    soucet += mv;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  float napeti = soucet / 16.0f;
  if (napeti <= 0 || napeti >= 3300) {
    ESP_LOGE(TAG, "Neplatne napeti termistoru: %.1f mV", napeti);
    Teplota = NAN;
    return;
  }
  float odpor = 10000.0f * napeti / (3300.0f - napeti);
  float teplota = 1.0f / (1.0f / (24.6f + 273.15f) + logf(odpor / 7900.0f) / 3950.0f) - 273.15f;
  posledniTeploty[indexMereni] = teplota;
  indexMereni = (indexMereni + 1) % 10;
  if (pocetMereni < 10) pocetMereni++;
  float soucetTeplot = 0;
  for (int i = 0; i < pocetMereni; i++) soucetTeplot += posledniTeploty[i];
  Teplota = soucetTeplot / pocetMereni;
}

static bool inicializujMpu() {
  for (int i = 0; i < 2; i++) {
    uint8_t reg = 0x75;
    uint8_t identita = 0;
    if (i2c_master_transmit_receive(mpu[i], &reg, 1, &identita, 1, 30) != ESP_OK) continue;
    if (identita != 0x70 && identita != 0x71 && identita != 0x73) continue;
    uint8_t probuzeni[] = {0x6B, 0x00};
    if (i2c_master_transmit(mpu[i], probuzeni, sizeof(probuzeni), 30) != ESP_OK) continue;
    aktivniMpu = i;
    vTaskDelay(pdMS_TO_TICKS(100));
    return true;
  }
  return false;
}

void updateNaklon() {
  if (aktivniMpu < 0) {
    if (esp_timer_get_time() < dalsiPokusMpu) return;
    if (!inicializujMpu()) {
      NaklonX = NAN;
      NaklonY = NAN;
      dalsiPokusMpu = esp_timer_get_time() + 1000000;
      if (!chybaMpu) ESP_LOGE(TAG, "MPU nenalezeno, kontrolujte zapojeni");
      chybaMpu = true;
      return;
    }
  }
  uint8_t reg = 0x3B;
  uint8_t data[6];
  esp_err_t err = i2c_master_transmit_receive(mpu[aktivniMpu], &reg, 1, data, sizeof(data), 30);
  if (err != ESP_OK) {
    if (!chybaMpu) ESP_LOGE(TAG, "Chyba cteni MPU: %s", esp_err_to_name(err));
    chybaMpu = true;
    NaklonX = NAN;
    NaklonY = NAN;
    aktivniMpu = -1;
    dalsiPokusMpu = esp_timer_get_time() + 1000000;
    return;
  }
  int16_t x = static_cast<int16_t>((uint16_t(data[0]) << 8) | data[1]);
  int16_t y = static_cast<int16_t>((uint16_t(data[2]) << 8) | data[3]);
  int16_t z = static_cast<int16_t>((uint16_t(data[4]) << 8) | data[5]);
  if (x == 0 && y == 0 && z == 0) {
    if (!chybaMpu) ESP_LOGE(TAG, "Neplatne mereni MPU");
    chybaMpu = true;
    NaklonX = NAN;
    NaklonY = NAN;
    return;
  }
  NaklonX = atan2f(float(y), float(z)) * 180.0f / 3.14159265358979323846f;
  NaklonY = atan2f(-float(x), sqrtf(float(y) * y + float(z) * z)) * 180.0f / 3.14159265358979323846f;
  if (chybaMpu) ESP_LOGI(TAG, "Mereni MPU obnoveno");
  chybaMpu = false;
}

static void ulozMaxima(int64_t ted) {
  for (auto &maximum : maxima) {
    if (!maximum.zmeneno || ted < maximum.dalsiPokus) continue;
    esp_err_t err = ulozHodnotu(maximum.nazev, *maximum.hodnota);
    if (err == ESP_OK) {
      maximum.zmeneno = false;
    } else {
      ESP_LOGE(TAG, "Nelze ulozit %s: %s", maximum.nazev, esp_err_to_name(err));
      maximum.dalsiPokus = ted + 5000000;
    }
  }
}

void updateMaxNaklony() {
  if (std::isfinite(NaklonX)) {
    if (NaklonX > MaxNaklonKladny) {
      MaxNaklonKladny = NaklonX;
      maxima[0].zmeneno = true;
    } else if (NaklonX < MaxNaklonZaporny) {
      MaxNaklonZaporny = NaklonX;
      maxima[1].zmeneno = true;
    }
  }
  ulozMaxima(esp_timer_get_time());
}

bool resetMaxNaklony() {
  for (auto &maximum : maxima) {
    *maximum.hodnota = 0;
    maximum.zmeneno = true;
    maximum.dalsiPokus = 0;
  }
  ulozMaxima(esp_timer_get_time());
  bool ulozeno = !maxima[0].zmeneno && !maxima[1].zmeneno;
  if (!ulozeno)
    ESP_LOGW(TAG, "Maxima vynulovana v pameti, ulozeni se zopakuje");
  else
    ESP_LOGI(TAG, "Maxima naklonu vynulovana a ulozena");
  return ulozeno;
}
