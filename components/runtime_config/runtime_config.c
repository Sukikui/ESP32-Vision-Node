#include "runtime_config.h"

#include <inttypes.h>
#include <string.h>

#include "app_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"

/* Store runtime overrides in their own namespace so they stay separate from ESP-IDF internal NVS users. */
#define RUNTIME_CONFIG_NAMESPACE "runtime_cfg"
#define RUNTIME_CONFIG_HEARTBEAT_KEY "hb_int_s"
#define RUNTIME_CONFIG_MOTION_ENABLED_KEY "motion_en"
#define RUNTIME_CONFIG_MOTION_WARMUP_KEY "motion_wu"
#define RUNTIME_CONFIG_MOTION_COOLDOWN_KEY "motion_cd"
#define RUNTIME_CONFIG_IR_ILLUMINATOR_MODE_KEY "ir_mode"

static const char *TAG = "runtime_config";

typedef struct {
    bool initialized;
    uint32_t heartbeat_interval_s;
    bool motion_detection_enabled;
    uint32_t motion_warmup_ms;
    uint32_t motion_cooldown_ms;
    ir_illuminator_mode_t ir_illuminator_mode;
} runtime_config_state_t;

static runtime_config_state_t s_state = {
    .initialized = false,
    .heartbeat_interval_s = APP_HEARTBEAT_INTERVAL_S,
    .motion_detection_enabled = APP_MOTION_DEFAULT_ENABLED,
    .motion_warmup_ms = APP_MOTION_DEFAULT_WARMUP_MS,
    .motion_cooldown_ms = APP_MOTION_DEFAULT_COOLDOWN_MS,
    .ir_illuminator_mode = APP_IR_ILLUMINATOR_DEFAULT_MODE,
};

/* Keep runtime validation aligned with the live heartbeat task expectations. */
static bool heartbeat_interval_is_valid(uint32_t interval_s)
{
    return interval_s >= 1 && interval_s <= 3600;
}

/* Match the ranges exposed in Kconfig so runtime validation stays consistent with build-time defaults. */
static bool motion_warmup_is_valid(uint32_t warmup_ms)
{
    return warmup_ms <= 120000;
}

/* Match the ranges exposed in Kconfig so runtime validation stays consistent with build-time defaults. */
static bool motion_cooldown_is_valid(uint32_t cooldown_ms)
{
    return cooldown_ms <= 60000;
}

/* Keep runtime validation aligned with the supported IR control policies documented for the firmware. */
static bool ir_illuminator_mode_is_valid(ir_illuminator_mode_t mode)
{
    return mode == IR_ILLUMINATOR_MODE_OFF
        || mode == IR_ILLUMINATOR_MODE_ON
        || mode == IR_ILLUMINATOR_MODE_CAPTURE;
}

/* Return true when the patch carries no changes at all. */
static bool runtime_config_patch_is_empty(const runtime_config_patch_t *patch)
{
    return !patch->has_heartbeat_interval_s
        && !patch->has_motion_detection_enabled
        && !patch->has_motion_warmup_ms
        && !patch->has_motion_cooldown_ms
        && !patch->has_ir_illuminator_mode;
}

/* Open the dedicated NVS namespace used by this component. */
static esp_err_t open_runtime_config_nvs(nvs_open_mode_t open_mode, nvs_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(out_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid NVS handle output");
    return nvs_open(RUNTIME_CONFIG_NAMESPACE, open_mode, out_handle);
}

/* Write one u32 key into an already opened NVS transaction. */
static esp_err_t nvs_write_u32_value(nvs_handle_t handle, const char *key, uint32_t value)
{
    esp_err_t err = nvs_set_u32(handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to persist key %s: %s", key, esp_err_to_name(err));
    }

    return err;
}

/* Write one boolean key into an already opened NVS transaction. */
static esp_err_t nvs_write_bool_value(nvs_handle_t handle, const char *key, bool value)
{
    esp_err_t err = nvs_set_u8(handle, key, value ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to persist key %s: %s", key, esp_err_to_name(err));
    }

    return err;
}

/* Validate the whole patch before any RAM or NVS state is modified. */
static esp_err_t runtime_config_validate_patch(const runtime_config_patch_t *patch)
{
    ESP_RETURN_ON_FALSE(patch != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid runtime config patch");

    if (patch->has_heartbeat_interval_s && !heartbeat_interval_is_valid(patch->heartbeat_interval_s)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!APP_HAS_MOTION_DETECTION
        && (patch->has_motion_detection_enabled || patch->has_motion_warmup_ms || patch->has_motion_cooldown_ms)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (patch->has_motion_warmup_ms && !motion_warmup_is_valid(patch->motion_warmup_ms)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (patch->has_motion_cooldown_ms && !motion_cooldown_is_valid(patch->motion_cooldown_ms)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!APP_HAS_IR_ILLUMINATOR && patch->has_ir_illuminator_mode) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (patch->has_ir_illuminator_mode && !ir_illuminator_mode_is_valid(patch->ir_illuminator_mode)) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

/* Persist all changed keys in one NVS transaction so cmd/config becomes all-or-nothing. */
static esp_err_t runtime_config_persist_patch(const runtime_config_patch_t *patch)
{
    nvs_handle_t handle = 0;
    esp_err_t err = open_runtime_config_nvs(NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    if (patch->has_heartbeat_interval_s) {
        err = nvs_write_u32_value(handle, RUNTIME_CONFIG_HEARTBEAT_KEY, patch->heartbeat_interval_s);
    }

    if (err == ESP_OK && patch->has_motion_detection_enabled) {
        err = nvs_write_bool_value(handle, RUNTIME_CONFIG_MOTION_ENABLED_KEY, patch->motion_detection_enabled);
    }

    if (err == ESP_OK && patch->has_motion_warmup_ms) {
        err = nvs_write_u32_value(handle, RUNTIME_CONFIG_MOTION_WARMUP_KEY, patch->motion_warmup_ms);
    }

    if (err == ESP_OK && patch->has_motion_cooldown_ms) {
        err = nvs_write_u32_value(handle, RUNTIME_CONFIG_MOTION_COOLDOWN_KEY, patch->motion_cooldown_ms);
    }

    if (err == ESP_OK && patch->has_ir_illuminator_mode) {
        err = nvs_write_u32_value(handle, RUNTIME_CONFIG_IR_ILLUMINATOR_MODE_KEY, (uint32_t)patch->ir_illuminator_mode);
    }

    if (err == ESP_OK) {
        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "failed to commit runtime config patch: %s", esp_err_to_name(err));
        }
    }

    nvs_close(handle);
    return err;
}

/* Mirror all changed keys from the patch into the in-memory source of truth. */
static void runtime_config_apply_patch_to_state(const runtime_config_patch_t *patch)
{
    if (patch->has_heartbeat_interval_s) {
        s_state.heartbeat_interval_s = patch->heartbeat_interval_s;
    }

    if (patch->has_motion_detection_enabled) {
        s_state.motion_detection_enabled = patch->motion_detection_enabled;
    }

    if (patch->has_motion_warmup_ms) {
        s_state.motion_warmup_ms = patch->motion_warmup_ms;
    }

    if (patch->has_motion_cooldown_ms) {
        s_state.motion_cooldown_ms = patch->motion_cooldown_ms;
    }

    if (patch->has_ir_illuminator_mode) {
        s_state.ir_illuminator_mode = patch->ir_illuminator_mode;
    }
}

/* Seed runtime values from Kconfig defaults, then overwrite them with any saved NVS values. */
esp_err_t runtime_config_init(void)
{
    nvs_handle_t handle = 0;
    uint32_t stored_heartbeat_interval_s = APP_HEARTBEAT_INTERVAL_S;
    uint32_t stored_motion_warmup_ms = APP_MOTION_DEFAULT_WARMUP_MS;
    uint32_t stored_motion_cooldown_ms = APP_MOTION_DEFAULT_COOLDOWN_MS;
    uint32_t stored_ir_illuminator_mode = (uint32_t)APP_IR_ILLUMINATOR_DEFAULT_MODE;
    uint8_t stored_motion_detection_enabled = APP_MOTION_DEFAULT_ENABLED ? 1 : 0;
    esp_err_t err;
    esp_err_t read_err;

    s_state.initialized = false;
    s_state.heartbeat_interval_s = APP_HEARTBEAT_INTERVAL_S;
    s_state.motion_detection_enabled = APP_MOTION_DEFAULT_ENABLED;
    s_state.motion_warmup_ms = APP_MOTION_DEFAULT_WARMUP_MS;
    s_state.motion_cooldown_ms = APP_MOTION_DEFAULT_COOLDOWN_MS;
    s_state.ir_illuminator_mode = APP_IR_ILLUMINATOR_DEFAULT_MODE;

    err = open_runtime_config_nvs(NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to open runtime config namespace: %s", esp_err_to_name(err));
        return err;
    }

    /*
     * Missing keys are expected on first boot, so they are not treated as errors.
     * Only validated values overwrite the compile-time defaults.
     */
    read_err = nvs_get_u32(handle, RUNTIME_CONFIG_HEARTBEAT_KEY, &stored_heartbeat_interval_s);
    if (read_err == ESP_OK) {
        if (heartbeat_interval_is_valid(stored_heartbeat_interval_s)) {
            s_state.heartbeat_interval_s = stored_heartbeat_interval_s;
            ESP_LOGI(TAG, "restored heartbeat_interval_s=%" PRIu32 " from NVS", stored_heartbeat_interval_s);
        } else {
            ESP_LOGW(TAG,
                     "ignoring invalid persisted heartbeat_interval_s=%" PRIu32 ", keeping default=%" PRIu32,
                     stored_heartbeat_interval_s,
                     APP_HEARTBEAT_INTERVAL_S);
        }
    } else if (read_err != ESP_ERR_NVS_NOT_FOUND) {
        err = read_err;
        ESP_LOGE(TAG, "failed to read heartbeat interval from NVS: %s", esp_err_to_name(read_err));
    }

    read_err = nvs_get_u8(handle, RUNTIME_CONFIG_MOTION_ENABLED_KEY, &stored_motion_detection_enabled);
    if (read_err == ESP_OK) {
        if (stored_motion_detection_enabled <= 1) {
            if (!APP_HAS_MOTION_DETECTION && stored_motion_detection_enabled != 0) {
                s_state.motion_detection_enabled = false;
                ESP_LOGW(TAG, "ignoring persisted motion_detection_enabled=true because PIR support is not compiled into this build");
            } else {
                s_state.motion_detection_enabled = (stored_motion_detection_enabled != 0);
                ESP_LOGI(TAG,
                         "restored motion_detection_enabled=%s from NVS",
                         s_state.motion_detection_enabled ? "true" : "false");
            }
        } else {
            ESP_LOGW(TAG,
                     "ignoring invalid persisted motion_detection_enabled=%u, keeping default=%s",
                     stored_motion_detection_enabled,
                     APP_MOTION_DEFAULT_ENABLED ? "true" : "false");
        }
    } else if (read_err != ESP_ERR_NVS_NOT_FOUND && err == ESP_OK) {
        err = read_err;
        ESP_LOGE(TAG, "failed to read motion enabled flag from NVS: %s", esp_err_to_name(read_err));
    }

    read_err = nvs_get_u32(handle, RUNTIME_CONFIG_MOTION_WARMUP_KEY, &stored_motion_warmup_ms);
    if (read_err == ESP_OK) {
        if (motion_warmup_is_valid(stored_motion_warmup_ms)) {
            s_state.motion_warmup_ms = stored_motion_warmup_ms;
            ESP_LOGI(TAG, "restored motion_warmup_ms=%" PRIu32 " from NVS", stored_motion_warmup_ms);
        } else {
            ESP_LOGW(TAG,
                     "ignoring invalid persisted motion_warmup_ms=%" PRIu32 ", keeping default=%" PRIu32,
                     stored_motion_warmup_ms,
                     APP_MOTION_DEFAULT_WARMUP_MS);
        }
    } else if (read_err != ESP_ERR_NVS_NOT_FOUND && err == ESP_OK) {
        err = read_err;
        ESP_LOGE(TAG, "failed to read motion warm-up from NVS: %s", esp_err_to_name(read_err));
    }

    read_err = nvs_get_u32(handle, RUNTIME_CONFIG_MOTION_COOLDOWN_KEY, &stored_motion_cooldown_ms);
    if (read_err == ESP_OK) {
        if (motion_cooldown_is_valid(stored_motion_cooldown_ms)) {
            s_state.motion_cooldown_ms = stored_motion_cooldown_ms;
            ESP_LOGI(TAG, "restored motion_cooldown_ms=%" PRIu32 " from NVS", stored_motion_cooldown_ms);
        } else {
            ESP_LOGW(TAG,
                     "ignoring invalid persisted motion_cooldown_ms=%" PRIu32 ", keeping default=%" PRIu32,
                     stored_motion_cooldown_ms,
                     APP_MOTION_DEFAULT_COOLDOWN_MS);
        }
    } else if (read_err != ESP_ERR_NVS_NOT_FOUND && err == ESP_OK) {
        err = read_err;
        ESP_LOGE(TAG, "failed to read motion cooldown from NVS: %s", esp_err_to_name(read_err));
    }

    read_err = nvs_get_u32(handle, RUNTIME_CONFIG_IR_ILLUMINATOR_MODE_KEY, &stored_ir_illuminator_mode);
    if (read_err == ESP_OK) {
        if (ir_illuminator_mode_is_valid((ir_illuminator_mode_t)stored_ir_illuminator_mode)) {
            if (!APP_HAS_IR_ILLUMINATOR && stored_ir_illuminator_mode != (uint32_t)IR_ILLUMINATOR_MODE_OFF) {
                s_state.ir_illuminator_mode = IR_ILLUMINATOR_MODE_OFF;
                ESP_LOGW(TAG,
                         "ignoring persisted ir_illuminator_mode=%s because IR illuminator support is not compiled into this build",
                         runtime_config_ir_illuminator_mode_to_string((ir_illuminator_mode_t)stored_ir_illuminator_mode));
            } else {
                s_state.ir_illuminator_mode = (ir_illuminator_mode_t)stored_ir_illuminator_mode;
                ESP_LOGI(TAG,
                         "restored ir_illuminator_mode=%s from NVS",
                         runtime_config_ir_illuminator_mode_to_string(s_state.ir_illuminator_mode));
            }
        } else {
            ESP_LOGW(TAG,
                     "ignoring invalid persisted ir_illuminator_mode=%" PRIu32 ", keeping default=%s",
                     stored_ir_illuminator_mode,
                     runtime_config_ir_illuminator_mode_to_string(APP_IR_ILLUMINATOR_DEFAULT_MODE));
        }
    } else if (read_err != ESP_ERR_NVS_NOT_FOUND && err == ESP_OK) {
        err = read_err;
        ESP_LOGE(TAG, "failed to read IR illuminator mode from NVS: %s", esp_err_to_name(read_err));
    }

    nvs_close(handle);

    if (err == ESP_OK) {
        s_state.initialized = true;
        ESP_LOGI(TAG, "runtime config ready");
    }

    return err;
}

/* Expose the current in-memory heartbeat interval used as the runtime source of truth. */
uint32_t runtime_config_get_heartbeat_interval_s(void)
{
    return s_state.heartbeat_interval_s;
}

/* Expose the runtime enable flag used when PIR support exists in this build. */
bool runtime_config_get_motion_detection_enabled(void)
{
    return APP_HAS_MOTION_DETECTION ? s_state.motion_detection_enabled : false;
}

/* Expose the current PIR warm-up period from runtime config. */
uint32_t runtime_config_get_motion_warmup_ms(void)
{
    return s_state.motion_warmup_ms;
}

/* Expose the current PIR cooldown period from runtime config. */
uint32_t runtime_config_get_motion_cooldown_ms(void)
{
    return s_state.motion_cooldown_ms;
}

/* Expose the active IR illuminator mode selected from defaults and optional NVS override. */
ir_illuminator_mode_t runtime_config_get_ir_illuminator_mode(void)
{
    return APP_HAS_IR_ILLUMINATOR ? s_state.ir_illuminator_mode : IR_ILLUMINATOR_MODE_OFF;
}

/* Update the in-memory value and optionally mirror it to NVS for the next boot. */
esp_err_t runtime_config_set_heartbeat_interval_s(uint32_t interval_s, bool persist)
{
    runtime_config_patch_t patch = {
        .has_heartbeat_interval_s = true,
        .heartbeat_interval_s = interval_s,
    };

    return runtime_config_apply_patch(&patch, persist);
}

/* Update the persisted runtime flag that enables or disables PIR detection. */
esp_err_t runtime_config_set_motion_detection_enabled(bool enabled, bool persist)
{
    runtime_config_patch_t patch = {
        .has_motion_detection_enabled = true,
        .motion_detection_enabled = enabled,
    };

    return runtime_config_apply_patch(&patch, persist);
}

/* Update the PIR warm-up period kept in RAM and optionally store it for the next reboot. */
esp_err_t runtime_config_set_motion_warmup_ms(uint32_t warmup_ms, bool persist)
{
    runtime_config_patch_t patch = {
        .has_motion_warmup_ms = true,
        .motion_warmup_ms = warmup_ms,
    };

    return runtime_config_apply_patch(&patch, persist);
}

/* Update the PIR cooldown period kept in RAM and optionally store it for the next reboot. */
esp_err_t runtime_config_set_motion_cooldown_ms(uint32_t cooldown_ms, bool persist)
{
    runtime_config_patch_t patch = {
        .has_motion_cooldown_ms = true,
        .motion_cooldown_ms = cooldown_ms,
    };

    return runtime_config_apply_patch(&patch, persist);
}

/* Update the IR illuminator runtime policy in RAM and optionally store it for the next reboot. */
esp_err_t runtime_config_set_ir_illuminator_mode(ir_illuminator_mode_t mode, bool persist)
{
    runtime_config_patch_t patch = {
        .has_ir_illuminator_mode = true,
        .ir_illuminator_mode = mode,
    };

    return runtime_config_apply_patch(&patch, persist);
}

/* Convert one IR illuminator mode into the stable string used in MQTT payloads and docs. */
const char *runtime_config_ir_illuminator_mode_to_string(ir_illuminator_mode_t mode)
{
    switch (mode) {
    case IR_ILLUMINATOR_MODE_OFF:
        return "off";
    case IR_ILLUMINATOR_MODE_ON:
        return "on";
    case IR_ILLUMINATOR_MODE_CAPTURE:
        return "capture";
    default:
        return "unknown";
    }
}

/* Parse one MQTT-facing mode string into the internal enum used by the runtime config. */
bool runtime_config_parse_ir_illuminator_mode(const char *text, ir_illuminator_mode_t *out_mode)
{
    if (text == NULL || out_mode == NULL) {
        return false;
    }

    if (strcmp(text, "off") == 0) {
        *out_mode = IR_ILLUMINATOR_MODE_OFF;
        return true;
    }

    if (strcmp(text, "on") == 0) {
        *out_mode = IR_ILLUMINATOR_MODE_ON;
        return true;
    }

    if (strcmp(text, "capture") == 0) {
        *out_mode = IR_ILLUMINATOR_MODE_CAPTURE;
        return true;
    }

    return false;
}

/* Validate and apply a group of runtime changes as one logical update. */
esp_err_t runtime_config_apply_patch(const runtime_config_patch_t *patch, bool persist)
{
    ESP_RETURN_ON_FALSE(s_state.initialized, ESP_ERR_INVALID_STATE, TAG, "runtime config not initialized");
    ESP_RETURN_ON_ERROR(runtime_config_validate_patch(patch), TAG, "invalid runtime config patch");

    if (runtime_config_patch_is_empty(patch)) {
        return ESP_OK;
    }

    if (persist) {
        ESP_RETURN_ON_ERROR(runtime_config_persist_patch(patch), TAG, "failed to store runtime config patch");
    }

    runtime_config_apply_patch_to_state(patch);
    return ESP_OK;
}
