#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/* Create the RAM-backed queue used for deferred MQTT publications. */
esp_err_t publish_queue_init(void);

/* Start the worker task that drains the publish queue. */
esp_err_t publish_queue_start(void);

/* Push one MQTT publication request into the queue. */
esp_err_t publish_queue_push(const char *topic, const void *data, size_t data_len, int qos, bool retain);
