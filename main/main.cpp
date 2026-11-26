#include "connect.h"
#include "display.h"
#include "sensors.h"
#include "vstupy.h"

#include <cmath>
#include <cstdio>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "telemetrie";
static constexpr char MQTT_TOPIC[] = "roundmotodisplay/telemetrie";
static constexpr int64_t INTERVAL_MQTT = 60000000;
static constexpr int64_t OPAKOVANI_MQTT = 5000000;

static esp_err_t formatujHodnotu(char *text, size_t velikost, float hodnota, const char *nazev) {
  int delka;
  if (std::isfinite(hodnota)) {
    delka = snprintf(text, velikost, "%.9g", double(hodnota));
  } else {
    ESP_LOGW(TAG, "Hodnota %s neni dostupna", nazev);
    delka = snprintf(text, velikost, "null");
  }
  return delka < 0 || static_cast<size_t>(delka) >= velikost ? ESP_ERR_INVALID_SIZE : ESP_OK;
}

static esp_err_t odesliTelemetrii() {
  char teplota[32];
  char plus[32];
  char minus[32];
  esp_err_t err = formatujHodnotu(teplota, sizeof(teplota), Teplota, "teplota");
  if (err == ESP_OK) err = formatujHodnotu(plus, sizeof(plus), MaxNaklonKladny, "max_naklon_plus");
  if (err == ESP_OK) err = formatujHodnotu(minus, sizeof(minus), MaxNaklonZaporny, "max_naklon_minus");
  if (err != ESP_OK) return err;
  char zprava[160];
  int delka = snprintf(zprava, sizeof(zprava),
                       "{\"teplota\":%s,\"max_naklon_plus\":%s,\"max_naklon_minus\":%s}",
                       teplota, plus, minus);
  if (delka < 0 || static_cast<size_t>(delka) >= sizeof(zprava)) return ESP_ERR_INVALID_SIZE;
  return mqttPublish(MQTT_TOPIC, zprava);
}

extern "C" void app_main() {
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(vstupyInit());
  ESP_ERROR_CHECK(sensorsInit());
  ESP_ERROR_CHECK(displayInit());
  drawScale();
  updateMaxScale(MaxNaklonZaporny, MaxNaklonKladny);
  drawCelsius();
  drawBtStatus(false);
  ESP_ERROR_CHECK(connectInit());

  bool predchoziBt = false;
  bool teplotaZmerena = false;
  bool teplotaVykreslena = false;
  float predchoziMaxZaporny = MaxNaklonZaporny;
  float predchoziMaxKladny = MaxNaklonKladny;
  int64_t posledniMereni = 0;
  int64_t posledniVykresleni = 0;
  int64_t dalsiOdeslani = esp_timer_get_time() + INTERVAL_MQTT;
  enum class Obrazovka { Hlavni, Drzeni, Potvrzeni, CekaniNaUvolneni };
  Obrazovka obrazovka = Obrazovka::Hlavni;
  int fazeResetu = -1;
  int64_t konecPotvrzeni = 0;
  for (;;) {
    int64_t ted = esp_timer_get_time();
    if (!teplotaZmerena || ted - posledniMereni >= 1000000) {
      posledniMereni = ted;
      updateTemp();
      teplotaZmerena = true;
    }
    updateNaklon();
    updateMaxNaklony();
    StavResetu reset = ctiReset();
    ted = esp_timer_get_time();
    bool obnovitDisplej = false;
    if (reset.reset) {
      bool ulozeno = resetMaxNaklony();
      drawResetScreen(ulozeno ? 2 : 3);
      konecPotvrzeni = esp_timer_get_time() + 700000;
      obrazovka = Obrazovka::Potvrzeni;
    } else if (obrazovka == Obrazovka::Potvrzeni) {
      if (ted >= konecPotvrzeni) {
        obnovitDisplej = true;
        obrazovka = reset.stisknuto ? Obrazovka::CekaniNaUvolneni : Obrazovka::Hlavni;
      }
    } else if (obrazovka == Obrazovka::CekaniNaUvolneni) {
      if (!reset.stisknuto) obrazovka = Obrazovka::Hlavni;
    } else if (reset.stisknuto) {
      int faze = reset.dobaDrzeni >= 1000000 ? 1 : 0;
      if (obrazovka != Obrazovka::Drzeni || faze != fazeResetu) {
        drawResetScreen(faze);
        fazeResetu = faze;
      }
      obrazovka = Obrazovka::Drzeni;
    } else if (obrazovka == Obrazovka::Drzeni) {
      obrazovka = Obrazovka::Hlavni;
      obnovitDisplej = true;
    }

    bool stav = btConnected();
    if (obnovitDisplej) {
      drawMainScreen(NaklonX, MaxNaklonZaporny, MaxNaklonKladny, Teplota, stav);
      predchoziMaxZaporny = MaxNaklonZaporny;
      predchoziMaxKladny = MaxNaklonKladny;
      predchoziBt = stav;
      teplotaVykreslena = true;
      posledniVykresleni = ted;
    } else if (obrazovka == Obrazovka::Hlavni || obrazovka == Obrazovka::CekaniNaUvolneni) {
      updateScale(NaklonX);
      if (MaxNaklonZaporny != predchoziMaxZaporny || MaxNaklonKladny != predchoziMaxKladny) {
        updateMaxScale(MaxNaklonZaporny, MaxNaklonKladny);
        predchoziMaxZaporny = MaxNaklonZaporny;
        predchoziMaxKladny = MaxNaklonKladny;
      }
      if (!teplotaVykreslena || ted - posledniVykresleni >= 20000000) {
        drawTeplota(Teplota);
        posledniVykresleni = ted;
        teplotaVykreslena = true;
      }
      if (stav != predchoziBt) {
        drawBtStatus(stav);
        predchoziBt = stav;
      }
    }
    ted = esp_timer_get_time();
    if (ted >= dalsiOdeslani && mqttConnected()) {
      esp_err_t err = odesliTelemetrii();
      if (err == ESP_OK) {
        ESP_LOGI(TAG, "Telemetrie zarazena k odeslani na %s", MQTT_TOPIC);
        dalsiOdeslani = ted + INTERVAL_MQTT;
      } else {
        ESP_LOGE(TAG, "Nelze odeslat telemetrii: %s", esp_err_to_name(err));
        dalsiOdeslani = ted + OPAKOVANI_MQTT;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
