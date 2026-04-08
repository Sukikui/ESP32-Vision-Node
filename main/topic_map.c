#include "topic_map.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"

static char s_node_id[APP_REQUEST_ID_MAX_LEN];
static char s_status_online_topic[APP_TOPIC_MAX_LEN];
static char s_status_heartbeat_topic[APP_TOPIC_MAX_LEN];
static char s_event_topic[APP_TOPIC_MAX_LEN];
static char s_command_subscription_topic[APP_TOPIC_MAX_LEN];
static char s_broadcast_subscription_topic[APP_TOPIC_MAX_LEN];
static char s_node_ping_topic[APP_TOPIC_MAX_LEN];
static char s_node_reboot_topic[APP_TOPIC_MAX_LEN];
static char s_node_config_topic[APP_TOPIC_MAX_LEN];
static char s_node_capture_topic[APP_TOPIC_MAX_LEN];
static char s_reply_base_topic[APP_TOPIC_MAX_LEN];

static const char *BROADCAST_PING_TOPIC = "vision/broadcast/cmd/ping";
static const char *BROADCAST_REBOOT_TOPIC = "vision/broadcast/cmd/reboot";
static const char *BROADCAST_CONFIG_TOPIC = "vision/broadcast/cmd/config";
static const char *BROADCAST_CAPTURE_TOPIC = "vision/broadcast/cmd/capture";

static esp_err_t format_checked(char *buffer, size_t buffer_len, const char *fmt, const char *node_id)
{
    int written = snprintf(buffer, buffer_len, fmt, node_id);

    if (written < 0 || (size_t)written >= buffer_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t topic_map_init(const char *node_id)
{
    if (node_id == NULL || node_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(node_id) >= sizeof(s_node_id)) {
        return ESP_ERR_INVALID_SIZE;
    }

    strlcpy(s_node_id, node_id, sizeof(s_node_id));

    if (format_checked(s_status_online_topic, sizeof(s_status_online_topic), "vision/nodes/%s/status/online", s_node_id) != ESP_OK ||
        format_checked(s_status_heartbeat_topic, sizeof(s_status_heartbeat_topic), "vision/nodes/%s/status/heartbeat", s_node_id) != ESP_OK ||
        format_checked(s_event_topic, sizeof(s_event_topic), "vision/nodes/%s/event", s_node_id) != ESP_OK ||
        format_checked(s_command_subscription_topic, sizeof(s_command_subscription_topic), "vision/nodes/%s/cmd/+", s_node_id) != ESP_OK ||
        format_checked(s_node_ping_topic, sizeof(s_node_ping_topic), "vision/nodes/%s/cmd/ping", s_node_id) != ESP_OK ||
        format_checked(s_node_reboot_topic, sizeof(s_node_reboot_topic), "vision/nodes/%s/cmd/reboot", s_node_id) != ESP_OK ||
        format_checked(s_node_config_topic, sizeof(s_node_config_topic), "vision/nodes/%s/cmd/config", s_node_id) != ESP_OK ||
        format_checked(s_node_capture_topic, sizeof(s_node_capture_topic), "vision/nodes/%s/cmd/capture", s_node_id) != ESP_OK ||
        format_checked(s_reply_base_topic, sizeof(s_reply_base_topic), "vision/nodes/%s/reply", s_node_id) != ESP_OK) {
        return ESP_ERR_INVALID_SIZE;
    }

    strlcpy(s_broadcast_subscription_topic, "vision/broadcast/cmd/+", sizeof(s_broadcast_subscription_topic));

    return ESP_OK;
}

const char *topic_map_get_node_id(void)
{
    return s_node_id;
}

const char *topic_map_get_status_online_topic(void)
{
    return s_status_online_topic;
}

const char *topic_map_get_status_heartbeat_topic(void)
{
    return s_status_heartbeat_topic;
}

const char *topic_map_get_event_topic(void)
{
    return s_event_topic;
}

const char *topic_map_get_command_subscription_topic(void)
{
    return s_command_subscription_topic;
}

const char *topic_map_get_broadcast_subscription_topic(void)
{
    return s_broadcast_subscription_topic;
}

bool topic_map_is_ping_topic(const char *topic)
{
    if (topic == NULL) {
        return false;
    }

    return strcmp(topic, s_node_ping_topic) == 0 || strcmp(topic, BROADCAST_PING_TOPIC) == 0;
}

bool topic_map_is_reboot_topic(const char *topic)
{
    if (topic == NULL) {
        return false;
    }

    return strcmp(topic, s_node_reboot_topic) == 0 || strcmp(topic, BROADCAST_REBOOT_TOPIC) == 0;
}

bool topic_map_is_config_topic(const char *topic)
{
    if (topic == NULL) {
        return false;
    }

    return strcmp(topic, s_node_config_topic) == 0 || strcmp(topic, BROADCAST_CONFIG_TOPIC) == 0;
}

bool topic_map_is_capture_topic(const char *topic)
{
    if (topic == NULL) {
        return false;
    }

    return strcmp(topic, s_node_capture_topic) == 0 || strcmp(topic, BROADCAST_CAPTURE_TOPIC) == 0;
}

esp_err_t topic_map_format_reply_topic(char *buffer, size_t buffer_len, const char *request_id)
{
    int written;

    if (buffer == NULL || buffer_len == 0 || request_id == NULL || request_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(buffer, buffer_len, "%s/%s", s_reply_base_topic, request_id);
    if (written < 0 || (size_t)written >= buffer_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t topic_map_format_image_meta_topic(char *buffer, size_t buffer_len, const char *capture_id)
{
    int written;

    if (buffer == NULL || buffer_len == 0 || capture_id == NULL || capture_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(buffer, buffer_len, "vision/nodes/%s/image/%s/meta", s_node_id, capture_id);
    if (written < 0 || (size_t)written >= buffer_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t topic_map_format_image_chunk_topic(char *buffer, size_t buffer_len, const char *capture_id, size_t index)
{
    int written;

    if (buffer == NULL || buffer_len == 0 || capture_id == NULL || capture_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(buffer, buffer_len, "vision/nodes/%s/image/%s/chunk/%u", s_node_id, capture_id, (unsigned)index);
    if (written < 0 || (size_t)written >= buffer_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t topic_map_format_image_done_topic(char *buffer, size_t buffer_len, const char *capture_id)
{
    int written;

    if (buffer == NULL || buffer_len == 0 || capture_id == NULL || capture_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(buffer, buffer_len, "vision/nodes/%s/image/%s/done", s_node_id, capture_id);
    if (written < 0 || (size_t)written >= buffer_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}
