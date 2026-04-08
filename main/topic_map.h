#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

esp_err_t topic_map_init(const char *node_id);
const char *topic_map_get_node_id(void);
const char *topic_map_get_status_online_topic(void);
const char *topic_map_get_status_heartbeat_topic(void);
const char *topic_map_get_event_topic(void);
const char *topic_map_get_command_subscription_topic(void);
const char *topic_map_get_broadcast_subscription_topic(void);
bool topic_map_is_ping_topic(const char *topic);
bool topic_map_is_reboot_topic(const char *topic);
bool topic_map_is_config_topic(const char *topic);
bool topic_map_is_capture_topic(const char *topic);
esp_err_t topic_map_format_reply_topic(char *buffer, size_t buffer_len, const char *request_id);
esp_err_t topic_map_format_image_meta_topic(char *buffer, size_t buffer_len, const char *capture_id);
esp_err_t topic_map_format_image_chunk_topic(char *buffer, size_t buffer_len, const char *capture_id, size_t index);
esp_err_t topic_map_format_image_done_topic(char *buffer, size_t buffer_len, const char *capture_id);
