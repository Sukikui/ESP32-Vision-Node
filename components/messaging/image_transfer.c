#include "image_transfer.h"

#include <stdio.h>
#include <stdint.h>

#include "app_config.h"
#include "mqtt_service.h"
#include "topic_map.h"

esp_err_t image_transfer_publish(const char *capture_id, const void *data, size_t data_len, const char *content_type)
{
    char topic[APP_TOPIC_MAX_LEN];
    char meta_payload[APP_JSON_PAYLOAD_MAX_LEN];
    char done_payload[APP_JSON_PAYLOAD_MAX_LEN];
    const uint8_t *bytes = (const uint8_t *)data;
    const char *mime_type = (content_type != NULL && content_type[0] != '\0') ? content_type : "application/octet-stream";
    size_t chunk_count;
    size_t chunk_index;

    if (capture_id == NULL || capture_id[0] == '\0' || (data == NULL && data_len > 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    chunk_count = (data_len + APP_IMAGE_CHUNK_SIZE - 1U) / APP_IMAGE_CHUNK_SIZE;

    if (topic_map_format_image_meta_topic(topic, sizeof(topic), capture_id) != ESP_OK) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (snprintf(meta_payload,
                 sizeof(meta_payload),
                 "{\"capture_id\":\"%s\",\"content_type\":\"%s\",\"total_size\":%u,\"chunk_size\":%u,\"chunk_count\":%u}",
                 capture_id,
                 mime_type,
                 (unsigned)data_len,
                 (unsigned)APP_IMAGE_CHUNK_SIZE,
                 (unsigned)chunk_count) >= (int)sizeof(meta_payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (mqtt_service_publish_json(topic, meta_payload, 1, false) != ESP_OK) {
        return ESP_FAIL;
    }

    for (chunk_index = 0; chunk_index < chunk_count; ++chunk_index) {
        size_t offset = chunk_index * APP_IMAGE_CHUNK_SIZE;
        size_t remaining = data_len - offset;
        size_t chunk_len = remaining > APP_IMAGE_CHUNK_SIZE ? APP_IMAGE_CHUNK_SIZE : remaining;

        if (topic_map_format_image_chunk_topic(topic, sizeof(topic), capture_id, chunk_index) != ESP_OK) {
            return ESP_ERR_INVALID_SIZE;
        }

        if (mqtt_service_publish_bytes(topic, bytes + offset, chunk_len, 0, false) != ESP_OK) {
            return ESP_FAIL;
        }
    }

    if (topic_map_format_image_done_topic(topic, sizeof(topic), capture_id) != ESP_OK) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (snprintf(done_payload,
                 sizeof(done_payload),
                 "{\"capture_id\":\"%s\",\"chunk_count\":%u,\"ok\":true}",
                 capture_id,
                 (unsigned)chunk_count) >= (int)sizeof(done_payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    return mqtt_service_publish_json(topic, done_payload, 1, false);
}
