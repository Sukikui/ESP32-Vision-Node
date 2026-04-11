#include "topic_map.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"

/* Cache all node-specific MQTT topics once so the rest of the code can reuse stable pointers. */
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

/* Format one topic string and fail if the configured topic buffer cannot contain it. */
static esp_err_t format_checked(char *buffer, size_t buffer_len, const char *fmt, const char *node_id)
{
    int written = snprintf(buffer, buffer_len, fmt, node_id);

    if (written < 0 || (size_t)written >= buffer_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

/* Match one command topic against its node-specific and broadcast forms. */
static bool command_topic_matches(const char *topic, const char *node_topic, const char *broadcast_topic)
{
    return topic != NULL && (strcmp(topic, node_topic) == 0 || strcmp(topic, broadcast_topic) == 0);
}

/* Expand all node-specific topic strings once from the configured node ID. */
esp_err_t topic_map_init(const char *node_id)
{
    if (node_id == NULL || node_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlcpy(s_node_id, node_id, sizeof(s_node_id)) >= sizeof(s_node_id)) {
        return ESP_ERR_INVALID_SIZE;
    }

    /*
     * Most call sites only need a const char *.
     * Prebuilding the strings here avoids repeating snprintf calls every time one topic is needed.
     */
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

/* Return the configured node ID. */
const char *topic_map_get_node_id(void)
{
    return s_node_id;
}

/* Return the retained topic used for both online publication and broker-side Last Will. */
const char *topic_map_get_status_online_topic(void)
{
    return s_status_online_topic;
}

/* Return the heartbeat topic. */
const char *topic_map_get_status_heartbeat_topic(void)
{
    return s_status_heartbeat_topic;
}

/* Return the one-off event topic. */
const char *topic_map_get_event_topic(void)
{
    return s_event_topic;
}

/* Return the wildcard node-specific command subscription topic. */
const char *topic_map_get_command_subscription_topic(void)
{
    return s_command_subscription_topic;
}

/* Return the wildcard broadcast command subscription topic. */
const char *topic_map_get_broadcast_subscription_topic(void)
{
    return s_broadcast_subscription_topic;
}

/* Return true when the topic is either the node-specific or broadcast ping command topic. */
bool topic_map_is_ping_topic(const char *topic)
{
    return command_topic_matches(topic, s_node_ping_topic, BROADCAST_PING_TOPIC);
}

/* Return true when the topic is either the node-specific or broadcast reboot command topic. */
bool topic_map_is_reboot_topic(const char *topic)
{
    return command_topic_matches(topic, s_node_reboot_topic, BROADCAST_REBOOT_TOPIC);
}

/* Return true when the topic is either the node-specific or broadcast config command topic. */
bool topic_map_is_config_topic(const char *topic)
{
    return command_topic_matches(topic, s_node_config_topic, BROADCAST_CONFIG_TOPIC);
}

/* Return true when the topic is either the node-specific or broadcast capture command topic. */
bool topic_map_is_capture_topic(const char *topic)
{
    return command_topic_matches(topic, s_node_capture_topic, BROADCAST_CAPTURE_TOPIC);
}

/* Format the reply topic that matches one request_id, for example vision/nodes/p4-001/reply/req-42. */
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

/* Format the metadata topic that opens one image transfer session. */
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

/* Format the topic that carries one chunk index inside an image transfer session. */
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

/* Format the topic that announces the end of one image transfer session. */
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
