#pragma once

#include <stdint.h>

#include "esp_err.h"

/* Start the periodic heartbeat publication task. */
esp_err_t heartbeat_task_start(void);

/* Update the heartbeat period used by the running task. */
esp_err_t heartbeat_task_set_interval_s(uint32_t interval_s);

/* Return the currently configured heartbeat period in seconds. */
uint32_t heartbeat_task_get_interval_s(void);
