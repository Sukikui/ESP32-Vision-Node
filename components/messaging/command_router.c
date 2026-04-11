#include "command_router.h"

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "ethernet_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "heartbeat_task.h"
#include "ir_illuminator.h"
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

/* Copy a pointer + length pair into a local NUL-terminated C string buffer. */
static void copy_text_or_empty(char *buffer, size_t buffer_len, const char *data, int data_len)
{
    size_t copy_len = 0;

    if (buffer_len == 0) {
        return;
    }

    buffer[0] = '\0';

    if (data == NULL || data_len <= 0) {
        return;
    }

    copy_len = (size_t)data_len;
    if (copy_len >= buffer_len) {
        copy_len = buffer_len - 1;
    }

    memcpy(buffer, data, copy_len);
    buffer[copy_len] = '\0';
}

/*
 * Extract one JSON string field using a tiny hand-written parser.
 * Command payloads stay deliberately small and flat, so this avoids pulling a full JSON dependency.
 */
static bool extract_json_string_field(const char *payload, const char *field_name, char *buffer, size_t buffer_len)
{
    char pattern[32];
    const char *start;
    const char *end;
    size_t length;

    if (payload == NULL || field_name == NULL || buffer == NULL || buffer_len == 0) {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", field_name);
    start = strstr(payload, pattern);
    if (start == NULL) {
        return false;
    }

    start = strchr(start, ':');
    if (start == NULL) {
        return false;
    }

    start = strchr(start, '"');
    if (start == NULL) {
        return false;
    }
    start++;

    end = strchr(start, '"');
    if (end == NULL) {
        return false;
    }

    length = (size_t)(end - start);
    if (length >= buffer_len) {
        length = buffer_len - 1;
    }

    memcpy(buffer, start, length);
    buffer[length] = '\0';
    return true;
}

/* Extract one unsigned integer field from the same small top-level JSON command payload. */
static bool extract_json_u32_field(const char *payload, const char *field_name, uint32_t *value)
{
    char pattern[32];
    const char *start;
    char *endptr = NULL;
    unsigned long parsed;

    if (payload == NULL || field_name == NULL || value == NULL) {
        return false;
    }

    if (payload[0] != '{') {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", field_name);
    start = strstr(payload, pattern);
    if (start == NULL) {
        return false;
    }

    start = strchr(start, ':');
    if (start == NULL) {
        return false;
    }

    start++;
    while (*start == ' ' || *start == '\t' || *start == '"') {
        start++;
    }

    parsed = strtoul(start, &endptr, 10);
    if (endptr == start) {
        return false;
    }

    *value = (uint32_t)parsed;
    return true;
}

/* Extract one JSON boolean field from the same small top-level JSON command payload. */
static bool extract_json_bool_field(const char *payload, const char *field_name, bool *value)
{
    char pattern[32];
    const char *start;

    if (payload == NULL || field_name == NULL || value == NULL) {
        return false;
    }

    if (payload[0] != '{') {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\"", field_name);
    start = strstr(payload, pattern);
    if (start == NULL) {
        return false;
    }

    start = strchr(start, ':');
    if (start == NULL) {
        return false;
    }

    start++;
    while (*start == ' ' || *start == '\t') {
        start++;
    }

    if (strncmp(start, "true", 4) == 0) {
        *value = true;
        return true;
    }

    if (strncmp(start, "false", 5) == 0) {
        *value = false;
        return true;
    }

    return false;
}

/* Read the request_id field used to choose the reply topic for this command. */
static void extract_request_id(const char *payload, char *request_id, size_t request_id_len)
{
    if (request_id_len == 0) {
        return;
    }

    request_id[0] = '\0';

    if (payload == NULL || payload[0] == '\0' || payload[0] != '{') {
        return;
    }

    extract_json_string_field(payload, "request_id", request_id, request_id_len);
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

/* Build and publish a point-in-time state snapshot in response to cmd/ping. */
static void handle_ping_command(const char *request_id)
{
    char payload[APP_JSON_PAYLOAD_MAX_LEN];
    char ip_address[16];
    uint64_t uptime_s = (uint64_t)(esp_timer_get_time() / 1000000ULL);

    ethernet_service_get_ipv4_string(ip_address, sizeof(ip_address));

    if (snprintf(
            payload,
            sizeof(payload),
            "{\"node_id\":\"%s\",\"ok\":true,\"ip\":\"%s\",\"uptime_s\":%" PRIu64 "}",
            topic_map_get_node_id(),
            ip_address,
            uptime_s) >= (int)sizeof(payload)) {
        ESP_LOGE(TAG, "ping reply payload too large");
        return;
    }

    if (publish_reply(request_id, payload, false) != ESP_OK) {
        ESP_LOGW(TAG, "failed to publish ping reply");
    }
}

/* Apply the currently supported runtime configuration keys from cmd/config. */
static void handle_config_command(const char *request_id, const char *payload_in)
{
    char payload[APP_JSON_PAYLOAD_MAX_LEN];
    char ir_mode_text[16];
    runtime_config_patch_t patch = {0};
    bool updated = false;
    esp_err_t err = ESP_OK;
    const char *error_code = "invalid_config";

    /*
     * Only the currently supported config keys are parsed here.
     * Unknown keys are ignored so the command stays forward-compatible while the firmware grows.
     */
    if (extract_json_u32_field(payload_in, "heartbeat_interval_s", &patch.heartbeat_interval_s)) {
        patch.has_heartbeat_interval_s = true;
    }

    if (extract_json_bool_field(payload_in, "motion_detection_enabled", &patch.motion_detection_enabled)) {
        patch.has_motion_detection_enabled = true;
    }

    if (extract_json_u32_field(payload_in, "motion_warmup_ms", &patch.motion_warmup_ms)) {
        patch.has_motion_warmup_ms = true;
    }

    if (extract_json_u32_field(payload_in, "motion_cooldown_ms", &patch.motion_cooldown_ms)) {
        patch.has_motion_cooldown_ms = true;
    }

    if (extract_json_string_field(payload_in, "ir_illuminator_mode", ir_mode_text, sizeof(ir_mode_text))) {
        if (runtime_config_parse_ir_illuminator_mode(ir_mode_text, &patch.ir_illuminator_mode)) {
            patch.has_ir_illuminator_mode = true;
        } else {
            err = ESP_ERR_INVALID_ARG;
        }
    }

    /*
     * Persist all requested config keys in one NVS transaction so a failing cmd/config
     * never leaves behind a half-applied persistent state.
     */
    if (err == ESP_OK) {
        err = runtime_config_apply_patch(&patch, true);
    }

    if (err == ESP_OK) {
        updated = patch.has_heartbeat_interval_s
            || patch.has_motion_detection_enabled
            || patch.has_motion_warmup_ms
            || patch.has_motion_cooldown_ms
            || patch.has_ir_illuminator_mode;

        if (patch.has_heartbeat_interval_s) {
            err = heartbeat_task_set_interval_s(runtime_config_get_heartbeat_interval_s());
        }

        if (err == ESP_OK && (patch.has_motion_detection_enabled || patch.has_motion_warmup_ms || patch.has_motion_cooldown_ms)) {
            err = motion_detection_apply_runtime_config();
        }

        if (err == ESP_OK && patch.has_ir_illuminator_mode) {
            err = ir_illuminator_apply_runtime_config();
        }

        if (err != ESP_OK) {
            error_code = "apply_failed";
        }
    } else if (err == ESP_ERR_NOT_SUPPORTED) {
        error_code = "unsupported_feature";
    } else if (err != ESP_ERR_INVALID_ARG) {
        error_code = "persist_failed";
    }

    if (err != ESP_OK) {
        if (snprintf(payload,
                     sizeof(payload),
                     "{\"node_id\":\"%s\",\"ok\":false,\"error\":\"%s\",\"heartbeat_interval_s\":%" PRIu32 ",\"motion_detection_enabled\":%s,\"motion_warmup_ms\":%" PRIu32 ",\"motion_cooldown_ms\":%" PRIu32 ",\"ir_illuminator_mode\":\"%s\"}",
                     topic_map_get_node_id(),
                     error_code,
                     runtime_config_get_heartbeat_interval_s(),
                     runtime_config_get_motion_detection_enabled() ? "true" : "false",
                     runtime_config_get_motion_warmup_ms(),
                     runtime_config_get_motion_cooldown_ms(),
                     runtime_config_ir_illuminator_mode_to_string(runtime_config_get_ir_illuminator_mode())) >= (int)sizeof(payload)) {
            ESP_LOGW(TAG, "config error reply payload too large");
            return;
        }
    } else {
        if (snprintf(payload,
                     sizeof(payload),
                     "{\"node_id\":\"%s\",\"ok\":true,\"heartbeat_interval_s\":%" PRIu32 ",\"motion_detection_enabled\":%s,\"motion_warmup_ms\":%" PRIu32 ",\"motion_cooldown_ms\":%" PRIu32 ",\"ir_illuminator_mode\":\"%s\",\"updated\":%s}",
                     topic_map_get_node_id(),
                     runtime_config_get_heartbeat_interval_s(),
                     runtime_config_get_motion_detection_enabled() ? "true" : "false",
                     runtime_config_get_motion_warmup_ms(),
                     runtime_config_get_motion_cooldown_ms(),
                     runtime_config_ir_illuminator_mode_to_string(runtime_config_get_ir_illuminator_mode()),
                     updated ? "true" : "false") >= (int)sizeof(payload)) {
            ESP_LOGW(TAG, "config success reply payload too large");
            return;
        }
        if (updated) {
            if (node_event_publish("config_updated") != ESP_OK) {
                ESP_LOGW(TAG, "failed to publish config_updated event");
            }
        }
    }

    if (publish_reply(request_id, payload, false) != ESP_OK) {
        ESP_LOGW(TAG, "failed to publish config reply");
    }
}

/* Return a deterministic failure until a real camera capture path exists. */
static void handle_capture_command(const char *request_id)
{
    char payload[APP_JSON_PAYLOAD_MAX_LEN];

    snprintf(payload,
             sizeof(payload),
             "{\"node_id\":\"%s\",\"ok\":false,\"error\":\"capture_not_implemented\"}",
             topic_map_get_node_id());

    publish_reply(request_id, payload, false);
    if (node_event_publish("capture_failed") != ESP_OK) {
        ESP_LOGW(TAG, "failed to publish capture_failed event");
    }
}

/* Prepare the reboot reply payload and hand the actual restart to a dedicated task. */
static void handle_reboot_command(const char *request_id)
{
    reboot_task_args_t *task_args = calloc(1, sizeof(*task_args));

    if (task_args == NULL) {
        ESP_LOGE(TAG, "failed to allocate reboot task args");
        return;
    }

    if (topic_map_format_reply_topic(task_args->reply_topic, sizeof(task_args->reply_topic), request_id) != ESP_OK) {
        free(task_args);
        ESP_LOGE(TAG, "failed to build reboot reply topic");
        return;
    }

    snprintf(task_args->payload,
             sizeof(task_args->payload),
             "{\"node_id\":\"%s\",\"ok\":true,\"rebooting\":true}",
             topic_map_get_node_id());

    if (xTaskCreate(reboot_task, "reboot_task", 3072, task_args, 8, NULL) != pdPASS) {
        free(task_args);
        ESP_LOGE(TAG, "failed to schedule reboot task");
    }
}

/* Normalize one incoming MQTT message into local strings, then dispatch it by topic. */
void command_router_handle(const char *topic, int topic_len, const char *data, int data_len)
{
    char topic_buffer[APP_TOPIC_MAX_LEN];
    char payload_buffer[APP_JSON_PAYLOAD_MAX_LEN];
    char request_id[APP_REQUEST_ID_MAX_LEN];

    /* esp-mqtt gives raw buffers plus lengths; copy them before using string APIs like strstr/strcmp. */
    copy_text_or_empty(topic_buffer, sizeof(topic_buffer), topic, topic_len);
    copy_text_or_empty(payload_buffer, sizeof(payload_buffer), data, data_len);
    extract_request_id(payload_buffer, request_id, sizeof(request_id));

    /* Every command reply topic is derived from request_id, so commands without it are rejected. */
    if (request_id[0] == '\0') {
        ESP_LOGW(TAG, "ignoring command without JSON request_id on topic: %s", topic_buffer);
        return;
    }

    if (topic_map_is_ping_topic(topic_buffer)) {
        handle_ping_command(request_id);
    } else if (topic_map_is_config_topic(topic_buffer)) {
        handle_config_command(request_id, payload_buffer);
    } else if (topic_map_is_reboot_topic(topic_buffer)) {
        handle_reboot_command(request_id);
    } else if (topic_map_is_capture_topic(topic_buffer)) {
        handle_capture_command(request_id);
    } else {
        ESP_LOGW(TAG, "unsupported command topic: %s", topic_buffer);
    }
}
