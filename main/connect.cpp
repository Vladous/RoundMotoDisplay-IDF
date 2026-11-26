#include "connect.h"
#include "ip.h"

#include <atomic>
#include <cstring>
#include <cstdlib>
#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_event.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_pan_api.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs.h"

static const char *TAG = "connect";
static constexpr const char *BROKER = secretIP;
static constexpr size_t ETH_HEADER_LEN = 14;
static std::atomic<bool> ipPripojeno(false);
static std::atomic<bool> mqttPripojeno(false);
static std::atomic<bool> zahlceno(false);
static std::atomic<uint16_t> panHandle(ESP_PAN_INVALID_HANDLE);
static esp_netif_t *netif = nullptr;
static esp_mqtt_client_handle_t mqtt = nullptr;
static QueueHandle_t udalosti = nullptr;
static esp_bd_addr_t telefon = {};
static bool ulozenyTelefon = false;

enum class Typ { Inicializace, Otevreno, Zavreno, Ip, BezIp };
struct Udalost {
  Typ typ;
  esp_pan_status_t status;
  uint16_t handle;
  esp_bd_addr_t adresa;
};

static void posliUdalost(const Udalost &udalost) {
  if (xQueueSend(udalosti, &udalost, 0) != pdTRUE) {
    ESP_LOGE(TAG, "Fronta sitovych udalosti je plna; restartuji pro obnovu spojeni");
    esp_restart();
  }
}

static esp_err_t vysliRamec(void *, void *data, size_t delka) {
  const uint16_t handle = panHandle.load();
  if (handle == ESP_PAN_INVALID_HANDLE || zahlceno.load() || !data ||
      delka < ETH_HEADER_LEN || delka - ETH_HEADER_LEN > ESP_PAN_MAX_WRITE_LEN) {
    return ESP_ERR_INVALID_STATE;
  }
  auto *ramec = static_cast<uint8_t *>(data);
  uint16_t protokol = (uint16_t(ramec[12]) << 8) | ramec[13];
  return esp_pan_write(handle, ramec, ramec + 6, protokol,
                       delka - ETH_HEADER_LEN, ramec + ETH_HEADER_LEN, false);
}

struct PanGlue {
  esp_netif_driver_base_t base;
  uint8_t mac[6];
};
static PanGlue glue = {};

static void uvolniRamec(void *, void *data) { free(data); }

static esp_err_t pripojNetif(esp_netif_t *rozhrani, void *argument) {
  auto *g = static_cast<PanGlue *>(argument);
  g->base.netif = rozhrani;
  esp_netif_driver_ifconfig_t cfg = {};
  cfg.handle = g;
  cfg.transmit = vysliRamec;
  cfg.driver_free_rx_buffer = uvolniRamec;
  esp_err_t err = esp_netif_set_driver_config(rozhrani, &cfg);
  return err == ESP_OK ? esp_netif_set_mac(rozhrani, g->mac) : err;
}

static void prijmiRamec(const esp_pan_cb_param_t::pan_data_ind_evt_param &data) {
  if (!netif || !data.data || !data.len || data.handle != panHandle.load()) return;
  size_t delka = ETH_HEADER_LEN + data.len;
  auto *ramec = static_cast<uint8_t *>(malloc(delka));
  if (!ramec) {
    ESP_LOGE(TAG, "Nedostatek pameti pro PAN ramec");
    return;
  }
  memcpy(ramec, data.dst, 6);
  memcpy(ramec + 6, data.src, 6);
  ramec[12] = data.protocol >> 8;
  ramec[13] = data.protocol & 0xff;
  memcpy(ramec + ETH_HEADER_LEN, data.data, data.len);
  esp_err_t err = esp_netif_receive(netif, ramec, delka, nullptr);
  if (err != ESP_OK) ESP_LOGW(TAG, "PAN prijem: %s", esp_err_to_name(err));
}

static void panCallback(esp_pan_cb_event_t event, esp_pan_cb_param_t *param) {
  Udalost u = {};
  switch (event) {
    case ESP_PAN_INIT_EVT:
      u.typ = Typ::Inicializace;
      u.status = param->init.status;
      break;
    case ESP_PAN_OPEN_EVT:
      u.typ = Typ::Otevreno;
      u.status = param->open.status;
      u.handle = param->open.handle;
      memcpy(u.adresa, param->open.remote_bda, sizeof(u.adresa));
      break;
    case ESP_PAN_CLOSE_EVT:
      ipPripojeno = false;
      mqttPripojeno = false;
      panHandle = ESP_PAN_INVALID_HANDLE;
      u.typ = Typ::Zavreno;
      u.handle = param->close.handle;
      break;
    case ESP_PAN_DATA_IND_EVT:
      prijmiRamec(param->data_ind);
      return;
    case ESP_PAN_CONG_EVT:
      zahlceno = param->cong.cong;
      return;
    default:
      return;
  }
  posliUdalost(u);
}

static void gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
  switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
      if (param->auth_cmpl.stat != ESP_BT_STATUS_SUCCESS)
        ESP_LOGE(TAG, "Parovani selhalo: %d", param->auth_cmpl.stat);
      else ESP_LOGI(TAG, "Bluetooth parovani dokonceno: %s", param->auth_cmpl.device_name);
      return;
    case ESP_BT_GAP_PIN_REQ_EVT: {
      esp_bt_pin_code_t pin = {};
      esp_err_t err = esp_bt_gap_pin_reply(param->pin_req.bda, false, 0, pin);
      ESP_LOGE(TAG, "Nezabezpecene starsi PIN parovani odmitnuto: %s", esp_err_to_name(err));
      return;
    }
    case ESP_BT_GAP_CFM_REQ_EVT: {
      esp_err_t err = esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, false);
      ESP_LOGE(TAG, "Nelze overit pozadovany parovaci kod: %s", esp_err_to_name(err));
      return;
    }
    default:
      return;
  }
}

static void ipCallback(void *, esp_event_base_t, int32_t event, void *data) {
  Udalost u = {};
  if (event == IP_EVENT_ETH_GOT_IP) {
    auto *ip = static_cast<ip_event_got_ip_t *>(data);
    if (ip->esp_netif != netif || panHandle.load() == ESP_PAN_INVALID_HANDLE) return;
    ESP_LOGI(TAG, "PAN IP " IPSTR, IP2STR(&ip->ip_info.ip));
    u.typ = Typ::Ip;
  } else {
    ipPripojeno = false;
    mqttPripojeno = false;
    u.typ = Typ::BezIp;
  }
  posliUdalost(u);
}

static void mqttCallback(void *, esp_event_base_t, int32_t event, void *data) {
  if (event == MQTT_EVENT_CONNECTED) {
    if (ipPripojeno.load()) {
      mqttPripojeno = true;
      ESP_LOGI(TAG, "MQTT pripojeno");
    }
  } else if (event == MQTT_EVENT_DISCONNECTED) {
    mqttPripojeno = false;
    ESP_LOGW(TAG, "MQTT odpojeno");
  } else if (event == MQTT_EVENT_ERROR) {
    auto *e = static_cast<esp_mqtt_event_handle_t>(data);
    if (e && e->error_handle) ESP_LOGE(TAG, "MQTT chyba typu %d", e->error_handle->error_type);
    else ESP_LOGE(TAG, "MQTT chyba bez podrobnosti");
  }
}

static void ulozTelefon() {
  nvs_handle_t nvs;
  esp_err_t err = nvs_open("pan", NVS_READWRITE, &nvs);
  if (err == ESP_OK) {
    err = nvs_set_blob(nvs, "peer", telefon, sizeof(telefon));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
  }
  if (err != ESP_OK) ESP_LOGE(TAG, "Nelze ulozit adresu telefonu: %s", esp_err_to_name(err));
  else ulozenyTelefon = true;
}

static void sitovaUloha(void *) {
  bool inicializovano = false;
  bool pripojovani = false;
  bool ukoncovani = false;
  bool mqttBezi = false;
  TickType_t dalsiPokus = 0;
  TickType_t dhcpDeadline = 0;
  TickType_t dalsiMqttPokus = 0;
  Udalost u;
  for (;;) {
    if (xQueueReceive(udalosti, &u, pdMS_TO_TICKS(1000)) == pdTRUE) {
      switch (u.typ) {
        case Typ::Inicializace:
          if (u.status != ESP_PAN_SUCCESS) {
            ESP_LOGE(TAG, "Inicializace PAN selhala: %d", u.status);
            vTaskDelete(nullptr);
          }
          inicializovano = true;
          dalsiPokus = xTaskGetTickCount();
          break;
        case Typ::Otevreno:
          pripojovani = false;
          if (u.status != ESP_PAN_SUCCESS || memcmp(telefon, u.adresa, sizeof(telefon)) != 0) {
            ESP_LOGE(TAG, "PAN spojeni selhalo nebo neodpovida telefonu: %d", u.status);
            if (u.status == ESP_PAN_SUCCESS) {
              esp_err_t err = esp_pan_disconnect(u.handle);
              if (err != ESP_OK) ESP_LOGE(TAG, "Nelze odpojit neznamy PAN: %s", esp_err_to_name(err));
            }
            dalsiPokus = xTaskGetTickCount() + pdMS_TO_TICKS(5000);
            break;
          }
          if (!ulozenyTelefon) ulozTelefon();
          {
            esp_err_t err = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
            if (err != ESP_OK) ESP_LOGE(TAG, "Nelze skryt Bluetooth: %s", esp_err_to_name(err));
          }
          zahlceno = false;
          panHandle = u.handle;
          ukoncovani = false;
          dhcpDeadline = xTaskGetTickCount() + pdMS_TO_TICKS(30000);
          ESP_ERROR_CHECK(esp_netif_set_default_netif(netif));
          esp_netif_action_start(netif, nullptr, 0, nullptr);
          esp_netif_action_connected(netif, nullptr, 0, nullptr);
          ESP_LOGI(TAG, "PAN pripojeno, cekam na DHCP");
          break;
        case Typ::Zavreno:
          panHandle = ESP_PAN_INVALID_HANDLE;
          ipPripojeno = false;
          mqttPripojeno = false;
          if (mqttBezi) {
            esp_err_t err = esp_mqtt_client_stop(mqtt);
            if (err != ESP_OK) ESP_LOGE(TAG, "Zastaveni MQTT: %s", esp_err_to_name(err));
            else mqttBezi = false;
          }
          esp_netif_action_disconnected(netif, nullptr, 0, nullptr);
          esp_netif_action_stop(netif, nullptr, 0, nullptr);
          {
            esp_err_t err = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
            if (err != ESP_OK) ESP_LOGE(TAG, "Nelze zviditelnit Bluetooth: %s", esp_err_to_name(err));
          }
          pripojovani = false;
          ukoncovani = false;
          dalsiPokus = xTaskGetTickCount() + pdMS_TO_TICKS(5000);
          ESP_LOGW(TAG, "PAN odpojeno");
          break;
        case Typ::Ip:
          if (panHandle.load() == ESP_PAN_INVALID_HANDLE) break;
          ipPripojeno = true;
          dalsiMqttPokus = xTaskGetTickCount();
          break;
        case Typ::BezIp:
          ipPripojeno = false;
          mqttPripojeno = false;
          dhcpDeadline = xTaskGetTickCount() + pdMS_TO_TICKS(30000);
          if (mqttBezi) {
            esp_err_t err = esp_mqtt_client_stop(mqtt);
            if (err != ESP_OK) ESP_LOGE(TAG, "Zastaveni MQTT: %s", esp_err_to_name(err));
            else mqttBezi = false;
          }
          break;
      }
    }
    if (panHandle.load() != ESP_PAN_INVALID_HANDLE && !ipPripojeno.load() &&
        !ukoncovani && static_cast<int32_t>(xTaskGetTickCount() - dhcpDeadline) >= 0) {
      ESP_LOGW(TAG, "PAN neziskalo IP adresu, obnovuji spojeni");
      esp_err_t err = esp_pan_disconnect(panHandle.load());
      if (err == ESP_OK) ukoncovani = true;
      else {
        ESP_LOGE(TAG, "Nelze ukoncit PAN: %s", esp_err_to_name(err));
        dhcpDeadline = xTaskGetTickCount() + pdMS_TO_TICKS(5000);
      }
    }
    if (!ipPripojeno.load() && mqttBezi &&
        static_cast<int32_t>(xTaskGetTickCount() - dalsiMqttPokus) >= 0) {
      esp_err_t err = esp_mqtt_client_stop(mqtt);
      if (err != ESP_OK) ESP_LOGE(TAG, "Zastaveni MQTT: %s", esp_err_to_name(err));
      else mqttBezi = false;
      dalsiMqttPokus = xTaskGetTickCount() + pdMS_TO_TICKS(5000);
    }
    if (ipPripojeno.load() && !mqttBezi &&
        static_cast<int32_t>(xTaskGetTickCount() - dalsiMqttPokus) >= 0) {
      esp_err_t err = esp_mqtt_client_start(mqtt);
      if (err != ESP_OK) ESP_LOGE(TAG, "Start MQTT: %s", esp_err_to_name(err));
      else mqttBezi = true;
      dalsiMqttPokus = xTaskGetTickCount() + pdMS_TO_TICKS(5000);
    }
    if (inicializovano && !pripojovani && panHandle.load() == ESP_PAN_INVALID_HANDLE &&
        static_cast<int32_t>(xTaskGetTickCount() - dalsiPokus) >= 0) {
      bool mamTelefon = ulozenyTelefon;
      if (!mamTelefon) {
        int pocet = esp_bt_gap_get_bond_device_num();
        if (pocet < 0) {
          ESP_LOGE(TAG, "Nelze zjistit sparovana Bluetooth zarizeni");
        } else if (pocet == 1) {
          esp_err_t err = esp_bt_gap_get_bond_device_list(&pocet, &telefon);
          if (err != ESP_OK) ESP_LOGE(TAG, "Nelze nacist sparovane zarizeni: %s", esp_err_to_name(err));
          else if (pocet == 1) mamTelefon = true;
        } else if (pocet == 0) {
          ESP_LOGI(TAG, "Cekam na sparovani telefonu s RoundMotoDisplay");
        } else {
          ESP_LOGE(TAG, "Je sparovano vice zarizeni; nelze vybrat telefon automaticky");
        }
      }
      if (mamTelefon) {
        ESP_LOGI(TAG, "Pripojuji PAN ke sparovanemu telefonu");
        esp_err_t err = esp_pan_connect(telefon, ESP_PAN_ROLE_PANU, ESP_PAN_ROLE_NAP);
        pripojovani = err == ESP_OK;
        if (err != ESP_OK) ESP_LOGE(TAG, "Bluetooth pokus selhal: %s", esp_err_to_name(err));
      }
      dalsiPokus = xTaskGetTickCount() + pdMS_TO_TICKS(5000);
    }
  }
}

esp_err_t connectInit() {
  esp_err_t err;
  nvs_handle_t nvs;
  err = nvs_open("pan", NVS_READONLY, &nvs);
  if (err == ESP_OK) {
    size_t delka = sizeof(telefon);
    err = nvs_get_blob(nvs, "peer", telefon, &delka);
    nvs_close(nvs);
    if (err == ESP_OK && delka == sizeof(telefon)) ulozenyTelefon = true;
    else if (err != ESP_ERR_NVS_NOT_FOUND) return err == ESP_OK ? ESP_ERR_INVALID_SIZE : err;
  } else if (err != ESP_ERR_NVS_NOT_FOUND) return err;

  udalosti = xQueueCreate(32, sizeof(Udalost));
  if (!udalosti) return ESP_ERR_NO_MEM;
  esp_mqtt_client_config_t mqttCfg = {};
  mqttCfg.broker.address.uri = BROKER;
  mqtt = esp_mqtt_client_init(&mqttCfg);
  if (!mqtt) return ESP_ERR_NO_MEM;
  if ((err = esp_mqtt_client_register_event(mqtt, MQTT_EVENT_ANY, mqttCallback, nullptr)) != ESP_OK) return err;
  if ((err = esp_netif_init()) != ESP_OK) return err;
  if ((err = esp_event_loop_create_default()) != ESP_OK) return err;
  if ((err = esp_bt_controller_mem_release(ESP_BT_MODE_BLE)) != ESP_OK) return err;
  esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  if ((err = esp_bt_controller_init(&cfg)) != ESP_OK) return err;
  if ((err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK) return err;
  esp_bluedroid_config_t blu = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
  if ((err = esp_bluedroid_init_with_cfg(&blu)) != ESP_OK) return err;
  if ((err = esp_bluedroid_enable()) != ESP_OK) return err;
  if ((err = esp_bt_gap_register_callback(gapCallback)) != ESP_OK) return err;
  if ((err = esp_pan_register_callback(panCallback)) != ESP_OK) return err;
  esp_bt_io_cap_t io = ESP_BT_IO_CAP_NONE;
  if ((err = esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &io, sizeof(io))) != ESP_OK) return err;
  if ((err = esp_bt_gap_set_device_name("RoundMotoDisplay")) != ESP_OK) return err;
  if ((err = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE)) != ESP_OK) return err;
  ESP_LOGI(TAG, "Bluetooth viditelny jako RoundMotoDisplay; cekam na sparovany telefon");
  glue.base.post_attach = pripojNetif;
  memcpy(glue.mac, esp_bt_dev_get_address(), sizeof(glue.mac));
  esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH();
  base.if_key = "PAN_DEF";
  base.if_desc = "pan";
  base.route_prio = 60;
  esp_netif_config_t netifCfg = {};
  netifCfg.base = &base;
  netifCfg.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;
  netif = esp_netif_new(&netifCfg);
  if (!netif) return ESP_ERR_NO_MEM;
  if ((err = esp_netif_attach(netif, &glue)) != ESP_OK) return err;
  if ((err = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, ipCallback, nullptr)) != ESP_OK) return err;
  if ((err = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, ipCallback, nullptr)) != ESP_OK) return err;
  if (xTaskCreate(sitovaUloha, "connect", 6144, nullptr, 5, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
  esp_pan_cfg_t panCfg = ESP_PAN_DEFAULT_CONFIG();
  return esp_pan_init(&panCfg);
}

bool btConnected() { return ipPripojeno.load(); }
bool mqttConnected() { return mqttPripojeno.load(); }

esp_err_t mqttPublish(const char *topic, const char *payload) {
  if (!topic || !*topic || !payload) return ESP_ERR_INVALID_ARG;
  if (!mqttConnected()) return ESP_ERR_INVALID_STATE;
  return esp_mqtt_client_enqueue(mqtt, topic, payload, 0, 1, 0, true) < 0 ? ESP_FAIL : ESP_OK;
}
