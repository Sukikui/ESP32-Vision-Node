#pragma once

#include <stdbool.h>

#include "app_config.h"
#include "esp_err.h"

/* Configure the GPIO that drives the IR illuminator and force a safe OFF state. */
esp_err_t ir_illuminator_init(void);

/* Re-read the current runtime mode and drive the output accordingly. */
esp_err_t ir_illuminator_apply_runtime_config(void);

/* Apply one explicit runtime mode to the live output without reading runtime_config. */
esp_err_t ir_illuminator_apply_mode(ir_illuminator_mode_t mode);

/* Tell the illuminator whether a capture is currently active for capture-coupled modes. */
esp_err_t ir_illuminator_set_capture_active(bool capture_active);

/* Return true when IR illuminator support is compiled into this firmware build. */
bool ir_illuminator_is_supported(void);

/* Return true when the illuminator output is currently driven ON. */
bool ir_illuminator_is_on(void);
