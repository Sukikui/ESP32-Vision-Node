#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/* Precompute all node-specific MQTT topic strings for the given node ID. */
esp_err_t topic_map_init(const char *node_id);

/* Return the configured node ID. */
const char *topic_map_get_node_id(void);

/* Return the retained online/offline status topic for this node. */
const char *topic_map_get_status_online_topic(void);

/* Return the periodic heartbeat topic for this node. */
const char *topic_map_get_status_heartbeat_topic(void);

/* Return the one-off event topic for this node. */
const char *topic_map_get_event_topic(void);

/* Return the wildcard subscription topic for node-specific commands. */
const char *topic_map_get_command_subscription_topic(void);

/* Return the wildcard subscription topic for broadcast commands. */
const char *topic_map_get_broadcast_subscription_topic(void);

/* Return true when the provided topic is a ping command for this node or broadcast. */
bool topic_map_is_ping_topic(const char *topic);

/* Return true when the provided topic is a reboot command for this node or broadcast. */
bool topic_map_is_reboot_topic(const char *topic);

/* Return true when the provided topic is a config command for this node or broadcast. */
bool topic_map_is_config_topic(const char *topic);

/* Return true when the provided topic is a capture command for this node or broadcast. */
bool topic_map_is_capture_topic(const char *topic);

/* Build the reply topic associated with one command request ID. */
esp_err_t topic_map_format_reply_topic(char *buffer, size_t buffer_len, const char *request_id);

/* Build the image metadata topic for one capture. */
esp_err_t topic_map_format_image_meta_topic(char *buffer, size_t buffer_len, const char *capture_id);

/* Build one chunk topic for a capture and chunk index. */
esp_err_t topic_map_format_image_chunk_topic(char *buffer, size_t buffer_len, const char *capture_id, size_t index);

/* Build the end-of-transfer topic for one capture. */
esp_err_t topic_map_format_image_done_topic(char *buffer, size_t buffer_len, const char *capture_id);
