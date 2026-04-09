#pragma once

#include <stdbool.h>

#include "sdkconfig.h"

/* Node and network configuration selected through menuconfig. */
#define APP_NODE_ID CONFIG_APP_NODE_ID
#define APP_HEARTBEAT_INTERVAL_S CONFIG_APP_HEARTBEAT_INTERVAL_S
#define APP_MQTT_BROKER_PORT CONFIG_APP_MQTT_BROKER_PORT

/* Fixed-size buffers used to keep topic strings in RAM. */
#define APP_TOPIC_MAX_LEN CONFIG_APP_TOPIC_MAX_LEN

/* Small JSON payloads are built in local stack buffers before publication. */
#define APP_JSON_PAYLOAD_MAX_LEN CONFIG_APP_JSON_PAYLOAD_MAX_LEN

/* Request IDs are copied out of incoming command payloads before routing. */
#define APP_REQUEST_ID_MAX_LEN CONFIG_APP_REQUEST_ID_MAX_LEN

/* The publish queue is RAM-backed, so both depth and item size stay bounded. */
#define APP_PUBLISH_QUEUE_LENGTH CONFIG_APP_PUBLISH_QUEUE_LENGTH
#define APP_PUBLISH_MAX_DATA_LEN CONFIG_APP_PUBLISH_MAX_DATA_LEN

/* Images are split into fixed-size MQTT chunks before publication. */
#define APP_IMAGE_CHUNK_SIZE CONFIG_APP_IMAGE_CHUNK_SIZE

#ifdef CONFIG_APP_HAS_MOTION_DETECTION
#define APP_HAS_MOTION_DETECTION 1
#else
#define APP_HAS_MOTION_DETECTION 0
#endif

#ifdef CONFIG_APP_MOTION_PIR_GPIO
#define APP_MOTION_PIR_GPIO CONFIG_APP_MOTION_PIR_GPIO
#else
#define APP_MOTION_PIR_GPIO 0
#endif

#ifdef CONFIG_APP_MOTION_PIR_ACTIVE_HIGH
#define APP_MOTION_PIR_ACTIVE_HIGH 1
#else
#define APP_MOTION_PIR_ACTIVE_HIGH 0
#endif

#ifdef CONFIG_APP_MOTION_DEFAULT_ENABLED
#define APP_MOTION_DEFAULT_ENABLED 1
#else
#define APP_MOTION_DEFAULT_ENABLED 0
#endif

#ifdef CONFIG_APP_MOTION_DEFAULT_WARMUP_MS
#define APP_MOTION_DEFAULT_WARMUP_MS CONFIG_APP_MOTION_DEFAULT_WARMUP_MS
#else
#define APP_MOTION_DEFAULT_WARMUP_MS 0
#endif

#ifdef CONFIG_APP_MOTION_DEFAULT_COOLDOWN_MS
#define APP_MOTION_DEFAULT_COOLDOWN_MS CONFIG_APP_MOTION_DEFAULT_COOLDOWN_MS
#else
#define APP_MOTION_DEFAULT_COOLDOWN_MS 0
#endif
