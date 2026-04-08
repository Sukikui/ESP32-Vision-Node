#include "node_event.h"

#include <inttypes.h>
#include <stdio.h>

#include "app_config.h"
#include "esp_timer.h"
#include "mqtt_service.h"
#include "topic_map.h"

esp_err_t node_event_publish(const char *event_name)
{
    char payload[APP_PAYLOAD_MAX_LEN];
    uint64_t timestamp_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
    int written;

    if (event_name == NULL || event_name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(payload,
                       sizeof(payload),
                       "{\"node_id\":\"%s\",\"event\":\"%s\",\"timestamp_ms\":%" PRIu64 "}",
                       topic_map_get_node_id(),
                       event_name,
                       timestamp_ms);
    if (written < 0 || (size_t)written >= sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    return mqtt_service_publish_json(topic_map_get_event_topic(), payload, 1, false);
}
