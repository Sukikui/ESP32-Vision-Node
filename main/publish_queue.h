#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

esp_err_t publish_queue_init(void);
esp_err_t publish_queue_start(void);
esp_err_t publish_queue_push(const char *topic, const void *data, size_t data_len, int qos, bool retain);
size_t publish_queue_get_depth(void);
