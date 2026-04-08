#include "image_transfer.h"

#include <stdio.h>
#include <stdint.h>

#include "app_config.h"
#include "mqtt_service.h"
#include "topic_map.h"

/* Publish one image as MQTT metadata, chunk stream, and completion marker. */
esp_err_t image_transfer_publish(const char *capture_id, const void *data, size_t data_len, const char *content_type)
{
    char topic[APP_TOPIC_MAX_LEN];
    char meta_payload[APP_JSON_PAYLOAD_MAX_LEN];
    char done_payload[APP_JSON_PAYLOAD_MAX_LEN];
    /*
     * Cast the generic image pointer to uint8_t * so the buffer can be addressed byte by byte.
     * That is required later when each chunk is published from bytes + offset.
     */
    const uint8_t *bytes = (const uint8_t *)data;
    /* The receiver needs a content type even if the caller does not provide one. */
    const char *mime_type = (content_type != NULL && content_type[0] != '\0') ? content_type : "application/octet-stream";
    size_t chunk_count;
    size_t chunk_index;

    /* Reject malformed transfer requests before building any topic or payload. */
    if (capture_id == NULL || capture_id[0] == '\0' || (data == NULL && data_len > 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    chunk_count = (data_len + APP_IMAGE_CHUNK_SIZE - 1U) / APP_IMAGE_CHUNK_SIZE;

    /* Build the MQTT topic used for the metadata message of this capture. */
    if (topic_map_format_image_meta_topic(topic, sizeof(topic), capture_id) != ESP_OK) {
        return ESP_ERR_INVALID_SIZE;
    }

    /*
     * Build the JSON metadata payload in meta_payload.
     * This is the first MQTT message of the transfer and it tells the receiver:
     * - which capture is starting
     * - how many total bytes the image contains
     * - how large each chunk can be
     * - how many chunk messages must be received before the transfer is complete
     */
    if (snprintf(meta_payload,
                 sizeof(meta_payload),
                 "{\"capture_id\":\"%s\",\"content_type\":\"%s\",\"total_size\":%u,\"chunk_size\":%u,\"chunk_count\":%u}",
                 capture_id,
                 mime_type,
                 (unsigned)data_len,
                 (unsigned)APP_IMAGE_CHUNK_SIZE,
                 (unsigned)chunk_count) >= (int)sizeof(meta_payload)) {
        /* If this fails, the configured JSON buffer is too small for the metadata contract. */
        return ESP_ERR_INVALID_SIZE;
    }

    /* Publish metadata first so the receiver can allocate/reconstruct before raw chunks arrive. */
    if (mqtt_service_publish_json(topic, meta_payload, 1, false) != ESP_OK) {
        return ESP_FAIL;
    }

    for (chunk_index = 0; chunk_index < chunk_count; ++chunk_index) {
        /* Compute which byte range of the original image buffer belongs to this chunk index. */
        size_t offset = chunk_index * APP_IMAGE_CHUNK_SIZE;
        size_t remaining = data_len - offset;
        /* The last chunk may be smaller than the fixed chunk size. */
        size_t chunk_len = remaining > APP_IMAGE_CHUNK_SIZE ? APP_IMAGE_CHUNK_SIZE : remaining;

        /* Encode the chunk index in the topic so the receiver can place bytes in the right order. */
        if (topic_map_format_image_chunk_topic(topic, sizeof(topic), capture_id, chunk_index) != ESP_OK) {
            return ESP_ERR_INVALID_SIZE;
        }

        /*
         * Publish this exact byte window on the chunk topic.
         * The payload is not wrapped in JSON: it is the raw binary content of the image slice
         * starting at bytes + offset and spanning chunk_len bytes.
         * Sending raw bytes avoids base64 expansion and keeps MQTT traffic smaller.
         */
        if (mqtt_service_publish_bytes(topic, bytes + offset, chunk_len, 0, false) != ESP_OK) {
            return ESP_FAIL;
        }
    }

    /* Build the MQTT topic used for the final "transfer complete" message. */
    if (topic_map_format_image_done_topic(topic, sizeof(topic), capture_id) != ESP_OK) {
        return ESP_ERR_INVALID_SIZE;
    }

    /*
     * Build the final JSON payload announcing that all chunks for this capture were sent.
     * The receiver can use chunk_count to verify that the transfer is complete.
     */
    if (snprintf(done_payload,
                 sizeof(done_payload),
                 "{\"capture_id\":\"%s\",\"chunk_count\":%u,\"ok\":true}",
                 capture_id,
                 (unsigned)chunk_count) >= (int)sizeof(done_payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Publish the end marker last so the receiver knows the transfer is complete. */
    return mqtt_service_publish_json(topic, done_payload, 1, false);
}
