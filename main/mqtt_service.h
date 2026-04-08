#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

esp_err_t mqtt_service_init(void);
esp_err_t mqtt_service_start(void);
bool mqtt_service_is_connected(void);
esp_err_t mqtt_service_publish_json(const char *topic, const char *json, int qos, bool retain);
esp_err_t mqtt_service_publish_bytes(const char *topic, const void *data, size_t data_len, int qos, bool retain);
esp_err_t mqtt_service_publish_immediate_json(const char *topic, const char *json, int qos, bool retain);
esp_err_t mqtt_service_publish_immediate_bytes(const char *topic, const void *data, size_t data_len, int qos, bool retain);
