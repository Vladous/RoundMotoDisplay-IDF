#pragma once

#include "esp_err.h"

esp_err_t connectInit();
bool btConnected();
bool mqttConnected();
esp_err_t mqttPublish(const char *topic, const char *payload);
