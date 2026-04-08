#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t heartbeat_task_start(void);
esp_err_t heartbeat_task_set_interval_s(uint32_t interval_s);
uint32_t heartbeat_task_get_interval_s(void);
