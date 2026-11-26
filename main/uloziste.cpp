#include "uloziste.h"

#include "nvs.h"

static constexpr char PROSTOR[] = "hodnoty";

esp_err_t ulozHodnotu(const char *nazev, float hodnota) {
  if (!nazev || !*nazev) return ESP_ERR_INVALID_ARG;
  nvs_handle_t nvs;
  esp_err_t err = nvs_open(PROSTOR, NVS_READWRITE, &nvs);
  if (err != ESP_OK) return err;
  err = nvs_set_blob(nvs, nazev, &hodnota, sizeof(hodnota));
  if (err == ESP_OK) err = nvs_commit(nvs);
  nvs_close(nvs);
  return err;
}

esp_err_t nactiHodnotu(const char *nazev, float *hodnota) {
  if (!nazev || !*nazev || !hodnota) return ESP_ERR_INVALID_ARG;
  nvs_handle_t nvs;
  esp_err_t err = nvs_open(PROSTOR, NVS_READONLY, &nvs);
  if (err != ESP_OK) return err;
  float nactena;
  size_t delka = sizeof(nactena);
  err = nvs_get_blob(nvs, nazev, &nactena, &delka);
  nvs_close(nvs);
  if (err != ESP_OK) return err;
  if (delka != sizeof(nactena)) return ESP_ERR_INVALID_SIZE;
  *hodnota = nactena;
  return ESP_OK;
}
