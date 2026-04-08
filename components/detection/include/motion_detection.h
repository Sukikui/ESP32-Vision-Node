#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(MOTION_DETECTION_EVENT);

#define MOTION_DETECTION_EVENT_TRIGGERED 1

esp_err_t motion_detection_init(void);
esp_err_t motion_detection_start(void);
bool motion_detection_is_enabled(void);
bool motion_detection_is_armed(void);
int64_t motion_detection_get_last_trigger_us(void);
