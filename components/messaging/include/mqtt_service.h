#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/* Initialize the MQTT client and derive the broker URI from DHCP state. */
esp_err_t mqtt_service_init(void);

/* Start the MQTT client after initialization. */
esp_err_t mqtt_service_start(void);

/* Return true when the MQTT session is currently connected. */
bool mqtt_service_is_connected(void);

/* Queue a JSON payload for publication through the RAM-backed publish queue. */
esp_err_t mqtt_service_publish_json(const char *topic, const char *json, int qos, bool retain);

/* Queue raw bytes for publication through the RAM-backed publish queue. */
esp_err_t mqtt_service_publish_bytes(const char *topic, const void *data, size_t data_len, int qos, bool retain);

/* Publish a JSON payload immediately, bypassing the publish queue. */
esp_err_t mqtt_service_publish_immediate_json(const char *topic, const char *json, int qos, bool retain);

/* Publish raw bytes immediately, bypassing the publish queue. */
esp_err_t mqtt_service_publish_immediate_bytes(const char *topic, const void *data, size_t data_len, int qos, bool retain);
