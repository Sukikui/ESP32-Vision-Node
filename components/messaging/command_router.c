#include "command_router.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "ethernet_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "heartbeat_task.h"
#include "ir_illuminator.h"
#include "json_utils.h"
#include "motion_detection.h"
#include "mqtt_service.h"
#include "node_event.h"
#include "runtime_config.h"
#include "topic_map.h"

/* Parse MQTT command payloads and dispatch them to the matching control-plane handlers. */
static const char *TAG = "command_router";

typedef struct {
    char reply_topic[APP_TOPIC_MAX_LEN];
    char payload[APP_JSON_PAYLOAD_MAX_LEN];
} reboot_task_args_t;

typedef struct {
    uint32_t heartbeat_interval_s;
    bool motion_detection_enabled;
    uint32_t motion_warmup_ms;
    uint32_t motion_cooldown_ms;
    ir_illuminator_mode_t ir_illuminator_mode;
} live_runtime_config_t;

/* Copy a pointer + length pair into a local NUL-terminated C string buffer. */
static bool copy_text_checked(char *buffer, size_t buffer_len, const char *data, int data_len)
{
    size_t copy_len = 0;

    if (buffer_len == 0) {
        return false;
    }

    buffer[0] = '\0';

    if (data == NULL || data_len <= 0) {
        return true;
    }

    copy_len = (size_t)data_len;
    if (copy_len >= buffer_len) {
        return false;
    }

    memcpy(buffer, data, copy_len);
    buffer[copy_len] = '\0';
    return true;
}

/* Extract one string field from a parsed JSON object and reject values that do not fit locally. */
static bool extract_json_string_field(const cJSON *root,
                                      const char *field_name,
                                      char *buffer,
                                      size_t buffer_len,
                                      bool *present)
{
    const cJSON *item;

    if (root == NULL || field_name == NULL || buffer == NULL || buffer_len == 0 || present == NULL) {
        return false;
    }

    *present = false;
    buffer[0] = '\0';

    item = cJSON_GetObjectItemCaseSensitive(root, field_name);
    if (item == NULL) {
        return true;
    }

    *present = true;
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }

    return strlcpy(buffer, item->valuestring, buffer_len) < buffer_len;
}

/* Extract one u32 field from a parsed JSON object and reject non-integral or out-of-range numbers. */
static bool extract_json_u32_field(const cJSON *root, const char *field_name, uint32_t *value, bool *present)
{
    const cJSON *item;
    double raw_value;
    uint32_t parsed_value;

    if (root == NULL || field_name == NULL || value == NULL || present == NULL) {
        return false;
    }

    *present = false;

    item = cJSON_GetObjectItemCaseSensitive(root, field_name);
    if (item == NULL) {
        return true;
    }

    *present = true;
    if (!cJSON_IsNumber(item)) {
        return false;
    }

    raw_value = item->valuedouble;
    if (raw_value < 0 || raw_value > (double)UINT32_MAX) {
        return false;
    }

    parsed_value = (uint32_t)raw_value;
    if ((double)parsed_value != raw_value) {
        return false;
    }

    *value = parsed_value;
    return true;
}

/* Extract one boolean field from a parsed JSON object. */
static bool extract_json_bool_field(const cJSON *root, const char *field_name, bool *value, bool *present)
{
    const cJSON *item;

    if (root == NULL || field_name == NULL || value == NULL || present == NULL) {
        return false;
    }

    *present = false;

    item = cJSON_GetObjectItemCaseSensitive(root, field_name);
    if (item == NULL) {
        return true;
    }

    *present = true;
    if (!cJSON_IsBool(item)) {
        return false;
    }

    *value = cJSON_IsTrue(item);
    return true;
}

/* Read the request_id field used to choose the reply topic for this command. */
static bool extract_request_id(const cJSON *payload_root, char *request_id, size_t request_id_len)
{
    bool present = false;

    if (request_id_len == 0) {
        return false;
    }

    request_id[0] = '\0';
    return extract_json_string_field(payload_root, "request_id", request_id, request_id_len, &present) && present;
}

/* Read the runtime values currently exposed by the rest of the firmware. */
static live_runtime_config_t read_live_runtime_config(void)
{
    live_runtime_config_t config = {
        .heartbeat_interval_s = runtime_config_get_heartbeat_interval_s(),
        .motion_detection_enabled = runtime_config_get_motion_detection_enabled(),
        .motion_warmup_ms = runtime_config_get_motion_warmup_ms(),
        .motion_cooldown_ms = runtime_config_get_motion_cooldown_ms(),
        .ir_illuminator_mode = runtime_config_get_ir_illuminator_mode(),
    };

    return config;
}

/* Overlay one validated patch onto an existing live config snapshot. */
static void apply_patch_to_live_runtime_config(live_runtime_config_t *config, const runtime_config_patch_t *patch)
{
    if (patch->has_heartbeat_interval_s) {
        config->heartbeat_interval_s = patch->heartbeat_interval_s;
    }

    if (patch->has_motion_detection_enabled) {
        config->motion_detection_enabled = patch->motion_detection_enabled;
    }

    if (patch->has_motion_warmup_ms) {
        config->motion_warmup_ms = patch->motion_warmup_ms;
    }

    if (patch->has_motion_cooldown_ms) {
        config->motion_cooldown_ms = patch->motion_cooldown_ms;
    }

    if (patch->has_ir_illuminator_mode) {
        config->ir_illuminator_mode = patch->ir_illuminator_mode;
    }
}

/* Push one whole runtime config snapshot into the live services that expose it. */
static esp_err_t apply_live_runtime_config(const live_runtime_config_t *config)
{
    esp_err_t err;

    err = motion_detection_apply_settings(config->motion_detection_enabled,
                                          config->motion_warmup_ms,
                                          config->motion_cooldown_ms);
    if (err != ESP_OK) {
        return err;
    }

    err = ir_illuminator_apply_mode(config->ir_illuminator_mode);
    if (err != ESP_OK) {
        return err;
    }

    return heartbeat_task_set_interval_s(config->heartbeat_interval_s);
}

/* Run reboot asynchronously so the reply message has time to leave the device before restart. */
static void reboot_task(void *arg)
{
    reboot_task_args_t *task_args = (reboot_task_args_t *)arg;

    mqtt_service_publish_immediate_json(task_args->reply_topic, task_args->payload, 1, false);
    free(task_args);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

/* Build vision/nodes/{node_id}/reply/{request_id} and publish the reply payload on it. */
static esp_err_t publish_reply(const char *request_id, const char *payload, bool immediate)
{
    char reply_topic[APP_TOPIC_MAX_LEN];

    ESP_RETURN_ON_ERROR(topic_map_format_reply_topic(reply_topic, sizeof(reply_topic), request_id), TAG, "failed to build reply topic");

    return immediate
        ? mqtt_service_publish_immediate_json(reply_topic, payload, 1, false)
        : mqtt_service_publish_json(reply_topic, payload, 1, false);
}

/* Render one JSON reply object into a fixed buffer and publish it on the request-specific reply topic. */
static esp_err_t publish_reply_json(const char *request_id, cJSON *root, bool immediate)
{
    char payload[APP_JSON_PAYLOAD_MAX_LEN];
    esp_err_t err = json_utils_print_to_buffer(root, payload, sizeof(payload));
    if (err == ESP_OK) {
        err = publish_reply(request_id, payload, immediate);
    }

    return err;
}

/* Start one reply payload with the common node_id and ok fields. */
static cJSON *create_reply_object(bool ok)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    cJSON_AddStringToObject(root, "node_id", topic_map_get_node_id());
    cJSON_AddBoolToObject(root, "ok", ok);
    return root;
}

/* Append the currently active runtime settings to one reply object. */
static void add_runtime_config_to_reply(cJSON *root)
{
    cJSON_AddNumberToObject(root, "heartbeat_interval_s", (double)runtime_config_get_heartbeat_interval_s());
    cJSON_AddBoolToObject(root, "motion_detection_enabled", runtime_config_get_motion_detection_enabled());
    cJSON_AddNumberToObject(root, "motion_warmup_ms", (double)runtime_config_get_motion_warmup_ms());
    cJSON_AddNumberToObject(root, "motion_cooldown_ms", (double)runtime_config_get_motion_cooldown_ms());
    cJSON_AddStringToObject(root, "ir_illuminator_mode",
                            runtime_config_ir_illuminator_mode_to_string(runtime_config_get_ir_illuminator_mode()));
}

/* Build and publish a point-in-time state snapshot in response to cmd/ping. */
static void handle_ping_command(const char *request_id)
{
    cJSON *root = NULL;
    char ip_address[16];
    uint64_t uptime_s = (uint64_t)(esp_timer_get_time() / 1000000ULL);

    ethernet_service_get_ipv4_string(ip_address, sizeof(ip_address));

    root = create_reply_object(true);
    if (root == NULL) {
        ESP_LOGE(TAG, "failed to allocate ping reply JSON");
        return;
    }

    cJSON_AddStringToObject(root, "ip", ip_address);
    cJSON_AddNumberToObject(root, "uptime_s", (double)uptime_s);

    if (publish_reply_json(request_id, root, false) != ESP_OK) {
        ESP_LOGW(TAG, "failed to publish ping reply");
    }

    cJSON_Delete(root);
}

/* Apply the currently supported runtime configuration keys from cmd/config. */
static void handle_config_command(const char *request_id, const cJSON *payload_root)
{
    char ir_mode_text[16];
    runtime_config_patch_t patch = {0};
    live_runtime_config_t previous_config = read_live_runtime_config();
    live_runtime_config_t next_config = previous_config;
    bool updated = false;
    bool present = false;
    esp_err_t err = ESP_OK;
    const char *error_code = "invalid_config";
    cJSON *reply_root = NULL;

    /*
     * Only the currently supported config keys are parsed here.
     * Unknown keys are ignored so the command stays forward-compatible while the firmware grows.
     */
    if (extract_json_u32_field(payload_root, "heartbeat_interval_s", &patch.heartbeat_interval_s, &present)) {
        patch.has_heartbeat_interval_s = present;
    } else {
        err = ESP_ERR_INVALID_ARG;
    }

    if (err == ESP_OK && extract_json_bool_field(payload_root, "motion_detection_enabled", &patch.motion_detection_enabled, &present)) {
        patch.has_motion_detection_enabled = present;
    } else if (err == ESP_OK) {
        err = ESP_ERR_INVALID_ARG;
    }

    if (err == ESP_OK && extract_json_u32_field(payload_root, "motion_warmup_ms", &patch.motion_warmup_ms, &present)) {
        patch.has_motion_warmup_ms = present;
    } else if (err == ESP_OK) {
        err = ESP_ERR_INVALID_ARG;
    }

    if (err == ESP_OK && extract_json_u32_field(payload_root, "motion_cooldown_ms", &patch.motion_cooldown_ms, &present)) {
        patch.has_motion_cooldown_ms = present;
    } else if (err == ESP_OK) {
        err = ESP_ERR_INVALID_ARG;
    }

    if (err == ESP_OK && extract_json_string_field(payload_root, "ir_illuminator_mode", ir_mode_text, sizeof(ir_mode_text), &present)) {
        if (present) {
            if (runtime_config_parse_ir_illuminator_mode(ir_mode_text, &patch.ir_illuminator_mode)) {
                patch.has_ir_illuminator_mode = true;
            } else {
                err = ESP_ERR_INVALID_ARG;
            }
        }
    } else if (err == ESP_OK) {
        err = ESP_ERR_INVALID_ARG;
    }

    if (err == ESP_OK) {
        err = runtime_config_validate_patch(&patch);
    }

    if (err == ESP_OK) {
        updated = patch.has_heartbeat_interval_s
            || patch.has_motion_detection_enabled
            || patch.has_motion_warmup_ms
            || patch.has_motion_cooldown_ms
            || patch.has_ir_illuminator_mode;

        if (updated) {
            apply_patch_to_live_runtime_config(&next_config, &patch);

            /*
             * Apply the whole staged snapshot first.
             * Only after the live services accept it do we persist the same patch to NVS.
             */
            err = apply_live_runtime_config(&next_config);
            if (err == ESP_OK) {
                err = runtime_config_apply_patch(&patch, true);
            }

            if (err != ESP_OK) {
                if (apply_live_runtime_config(&previous_config) != ESP_OK) {
                    ESP_LOGW(TAG, "failed to roll back live runtime config");
                }

                error_code = "apply_failed";
            }
        }
    } else if (err == ESP_ERR_NOT_SUPPORTED) {
        error_code = "unsupported_feature";
    } else if (err != ESP_ERR_INVALID_ARG) {
        error_code = "apply_failed";
    }

    reply_root = create_reply_object(err == ESP_OK);

    if (reply_root == NULL) {
        ESP_LOGW(TAG, "failed to allocate config reply JSON");
        return;
    }

    if (err != ESP_OK) {
        cJSON_AddStringToObject(reply_root, "error", error_code);
    }

    add_runtime_config_to_reply(reply_root);

    if (err == ESP_OK) {
        cJSON_AddBoolToObject(reply_root, "updated", updated);
        if (updated) {
            if (node_event_publish("config_updated") != ESP_OK) {
                ESP_LOGW(TAG, "failed to publish config_updated event");
            }
        }
    }

    if (publish_reply_json(request_id, reply_root, false) != ESP_OK) {
        ESP_LOGW(TAG, "failed to publish config reply");
    }

    cJSON_Delete(reply_root);
}

/* Return a deterministic failure until a real camera capture path exists. */
static void handle_capture_command(const char *request_id)
{
    cJSON *root = create_reply_object(false);

    if (root == NULL) {
        ESP_LOGE(TAG, "failed to allocate capture reply JSON");
        return;
    }

    cJSON_AddStringToObject(root, "error", "capture_not_implemented");

    if (publish_reply_json(request_id, root, false) != ESP_OK) {
        ESP_LOGW(TAG, "failed to publish capture reply");
    }
    if (node_event_publish("capture_failed") != ESP_OK) {
        ESP_LOGW(TAG, "failed to publish capture_failed event");
    }

    cJSON_Delete(root);
}

/* Prepare the reboot reply payload and hand the actual restart to a dedicated task. */
static void handle_reboot_command(const char *request_id)
{
    reboot_task_args_t *task_args = calloc(1, sizeof(*task_args));
    cJSON *root = NULL;

    if (task_args == NULL) {
        ESP_LOGE(TAG, "failed to allocate reboot task args");
        return;
    }

    if (topic_map_format_reply_topic(task_args->reply_topic, sizeof(task_args->reply_topic), request_id) != ESP_OK) {
        free(task_args);
        ESP_LOGE(TAG, "failed to build reboot reply topic");
        return;
    }

    root = create_reply_object(true);
    if (root == NULL) {
        free(task_args);
        ESP_LOGE(TAG, "failed to allocate reboot reply JSON");
        return;
    }

    cJSON_AddBoolToObject(root, "rebooting", true);

    if (json_utils_print_to_buffer(root, task_args->payload, sizeof(task_args->payload)) != ESP_OK) {
        cJSON_Delete(root);
        free(task_args);
        ESP_LOGE(TAG, "failed to render reboot reply payload");
        return;
    }

    cJSON_Delete(root);

    if (xTaskCreate(reboot_task, "reboot_task", 3072, task_args, 8, NULL) != pdPASS) {
        free(task_args);
        ESP_LOGE(TAG, "failed to schedule reboot task");
    }
}

/* Normalize one incoming MQTT message into local strings, then dispatch it by topic. */
void command_router_handle(const char *topic, int topic_len, const char *data, int data_len)
{
    char topic_buffer[APP_TOPIC_MAX_LEN];
    char request_id[APP_REQUEST_ID_MAX_LEN];
    cJSON *payload_root = NULL;

    /* esp-mqtt gives raw buffers plus lengths; copy them before using string APIs like strcmp and cJSON_Parse. */
    if (!copy_text_checked(topic_buffer, sizeof(topic_buffer), topic, topic_len)) {
        ESP_LOGW(TAG, "ignoring command with oversized topic (%d bytes)", topic_len);
        return;
    }

    if (!topic_map_is_ping_topic(topic_buffer)
        && !topic_map_is_config_topic(topic_buffer)
        && !topic_map_is_reboot_topic(topic_buffer)
        && !topic_map_is_capture_topic(topic_buffer)) {
        ESP_LOGW(TAG, "unsupported command topic: %s", topic_buffer);
        return;
    }

    if (data == NULL || data_len <= 0) {
        ESP_LOGW(TAG, "ignoring command with empty payload on topic: %s", topic_buffer);
        return;
    }

    if ((size_t)data_len >= APP_JSON_PAYLOAD_MAX_LEN) {
        ESP_LOGW(TAG, "ignoring command with oversized payload on topic %s (%d bytes)", topic_buffer, data_len);
        return;
    }

    /*
     * esp-mqtt does not NUL-terminate payloads.
     * Parse directly from the pointer + length pair instead of copying the whole payload first.
     */
    payload_root = cJSON_ParseWithLength(data, (size_t)data_len);
    if (payload_root == NULL || !cJSON_IsObject(payload_root)) {
        ESP_LOGW(TAG, "ignoring command with invalid JSON payload on topic: %s", topic_buffer);
        cJSON_Delete(payload_root);
        return;
    }

    if (!extract_request_id(payload_root, request_id, sizeof(request_id))) {
        ESP_LOGW(TAG, "ignoring command without valid JSON request_id on topic: %s", topic_buffer);
        cJSON_Delete(payload_root);
        return;
    }

    if (topic_map_is_ping_topic(topic_buffer)) {
        handle_ping_command(request_id);
    } else if (topic_map_is_config_topic(topic_buffer)) {
        handle_config_command(request_id, payload_root);
    } else if (topic_map_is_reboot_topic(topic_buffer)) {
        handle_reboot_command(request_id);
    } else if (topic_map_is_capture_topic(topic_buffer)) {
        handle_capture_command(request_id);
    }

    cJSON_Delete(payload_root);
}
