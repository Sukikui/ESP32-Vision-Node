#include "node_event.h"

#include <inttypes.h>

#include "app_config.h"
#include "cJSON.h"
#include "esp_timer.h"
#include "json_utils.h"
#include "mqtt_service.h"
#include "topic_map.h"

/* Build a small JSON event payload and publish it on the shared node event topic. */
esp_err_t node_event_publish(const char *event_name)
{
    cJSON *root;
    char payload[APP_JSON_PAYLOAD_MAX_LEN];
    uint64_t timestamp_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
    esp_err_t err;

    if (event_name == NULL || event_name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "node_id", topic_map_get_node_id());
    cJSON_AddStringToObject(root, "event", event_name);
    /* The timestamp is captured locally so subscribers can order events even if delivery is delayed. */
    cJSON_AddNumberToObject(root, "timestamp_ms", (double)timestamp_ms);

    err = json_utils_print_to_buffer(root, payload, sizeof(payload));
    if (err == ESP_OK) {
        err = mqtt_service_publish_json(topic_map_get_event_topic(), payload, 1, false);
    }

    cJSON_Delete(root);
    return err;
}
