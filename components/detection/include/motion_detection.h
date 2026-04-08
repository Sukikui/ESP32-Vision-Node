#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(MOTION_DETECTION_EVENT);

/* Event ID posted whenever a PIR trigger survives warm-up and cooldown checks. */
#define MOTION_DETECTION_EVENT_TRIGGERED 1

/* Configure the PIR GPIO, ISR, and worker task. */
esp_err_t motion_detection_init(void);

/* Arm PIR detection after initialization and start the warm-up timer. */
esp_err_t motion_detection_start(void);

/* Return true when PIR support is enabled in menuconfig. */
bool motion_detection_is_enabled(void);

/* Return true once the detector is enabled, started, and past warm-up. */
bool motion_detection_is_armed(void);

/* Return the timestamp of the last accepted trigger in microseconds. */
int64_t motion_detection_get_last_trigger_us(void);
