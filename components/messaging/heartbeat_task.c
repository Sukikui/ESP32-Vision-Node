#include "heartbeat_task.h"

#include <inttypes.h>
#include <stdio.h>

#include "app_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ethernet_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_service.h"
#include "topic_map.h"

static const char *TAG = "heartbeat_task";
static TaskHandle_t s_task_handle;
static volatile uint32_t s_interval_s = APP_HEARTBEAT_INTERVAL_S;

static void heartbeat_task(void *arg)
{
    char payload[APP_JSON_PAYLOAD_MAX_LEN];
    char ip_address[16];

    while (true) {
        uint64_t uptime_s = (uint64_t)(esp_timer_get_time() / 1000000ULL);

        ethernet_service_get_ipv4_string(ip_address, sizeof(ip_address));

        if (snprintf(
                payload,
                sizeof(payload),
                "{\"node_id\":\"%s\",\"ip\":\"%s\",\"uptime_s\":%" PRIu64 "}",
                topic_map_get_node_id(),
                ip_address,
                uptime_s) < (int)sizeof(payload)) {
            mqtt_service_publish_json(topic_map_get_status_heartbeat_topic(), payload, 0, false);
        } else {
            ESP_LOGE(TAG, "heartbeat payload too large");
        }

        vTaskDelay(pdMS_TO_TICKS(s_interval_s * 1000));
    }
}

esp_err_t heartbeat_task_start(void)
{
    if (s_task_handle != NULL) {
        return ESP_OK;
    }

    BaseType_t result = xTaskCreate(heartbeat_task, "heartbeat_task", 4096, NULL, 5, &s_task_handle);
    if (result != pdPASS) {
        s_task_handle = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t heartbeat_task_set_interval_s(uint32_t interval_s)
{
    if (interval_s == 0 || interval_s > 3600) {
        return ESP_ERR_INVALID_ARG;
    }

    s_interval_s = interval_s;
    return ESP_OK;
}

uint32_t heartbeat_task_get_interval_s(void)
{
    return s_interval_s;
}
