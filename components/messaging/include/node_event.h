#pragma once

#include "esp_err.h"

/* Publish a one-off node event on the shared MQTT event topic. */
esp_err_t node_event_publish(const char *event_name);
