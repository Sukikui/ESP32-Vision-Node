#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "esp_err.h"

typedef struct {
    bool has_heartbeat_interval_s;
    uint32_t heartbeat_interval_s;
    bool has_motion_detection_enabled;
    bool motion_detection_enabled;
    bool has_motion_warmup_ms;
    uint32_t motion_warmup_ms;
    bool has_motion_cooldown_ms;
    uint32_t motion_cooldown_ms;
    bool has_ir_illuminator_mode;
    ir_illuminator_mode_t ir_illuminator_mode;
} runtime_config_patch_t;

/* Load persisted runtime overrides from NVS and fall back to compile-time defaults. */
esp_err_t runtime_config_init(void);

/* Return the active heartbeat interval after defaults and any NVS override have been applied. */
uint32_t runtime_config_get_heartbeat_interval_s(void);

/* Return the current runtime enable flag for PIR detection when this build supports it. */
bool runtime_config_get_motion_detection_enabled(void);

/* Return the active PIR warm-up period after defaults and any NVS override have been applied. */
uint32_t runtime_config_get_motion_warmup_ms(void);

/* Return the active PIR cooldown period after defaults and any NVS override have been applied. */
uint32_t runtime_config_get_motion_cooldown_ms(void);

/* Return the active IR illuminator mode after defaults and any NVS override have been applied. */
ir_illuminator_mode_t runtime_config_get_ir_illuminator_mode(void);

/* Convert one IR illuminator mode into the MQTT-friendly string used in docs and replies. */
const char *runtime_config_ir_illuminator_mode_to_string(ir_illuminator_mode_t mode);

/* Parse one MQTT-friendly IR illuminator mode string into the internal enum. */
bool runtime_config_parse_ir_illuminator_mode(const char *text, ir_illuminator_mode_t *out_mode);

/* Validate one runtime patch without modifying RAM state or persisting anything. */
esp_err_t runtime_config_validate_patch(const runtime_config_patch_t *patch);

/* Validate and apply a group of runtime changes as one logical update. */
esp_err_t runtime_config_apply_patch(const runtime_config_patch_t *patch, bool persist);
