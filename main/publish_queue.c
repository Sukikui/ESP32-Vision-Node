#include "publish_queue.h"

#include <stdint.h>
#include <string.h>

#include "app_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mqtt_service.h"

typedef struct {
    char topic[APP_TOPIC_MAX_LEN];
    size_t data_len;
    int qos;
    bool retain;
    uint8_t data[APP_PUBLISH_MAX_DATA_LEN];
} publish_queue_item_t;

static const char *TAG = "publish_queue";

static QueueHandle_t s_queue;
static TaskHandle_t s_task_handle;

static void publish_queue_task(void *arg)
{
    publish_queue_item_t item;

    while (true) {
        if (xQueueReceive(s_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        while (mqtt_service_publish_immediate_bytes(item.topic, item.data, item.data_len, item.qos, item.retain) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }
}

esp_err_t publish_queue_init(void)
{
    if (s_queue != NULL) {
        return ESP_OK;
    }

    s_queue = xQueueCreate(APP_PUBLISH_QUEUE_LENGTH, sizeof(publish_queue_item_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t publish_queue_start(void)
{
    if (s_task_handle != NULL) {
        return ESP_OK;
    }

    if (s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xTaskCreate(publish_queue_task, "publish_queue_task", 4096, NULL, 6, &s_task_handle) != pdPASS) {
        s_task_handle = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t publish_queue_push(const char *topic, const void *data, size_t data_len, int qos, bool retain)
{
    publish_queue_item_t item = {0};

    if (s_queue == NULL || topic == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (strlen(topic) >= sizeof(item.topic) || data_len > sizeof(item.data)) {
        return ESP_ERR_INVALID_SIZE;
    }

    strlcpy(item.topic, topic, sizeof(item.topic));
    item.data_len = data_len;
    item.qos = qos;
    item.retain = retain;

    if (data != NULL && data_len > 0) {
        memcpy(item.data, data, data_len);
    }

    if (xQueueSend(s_queue, &item, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGW(TAG, "publish queue full for topic %s", topic);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

size_t publish_queue_get_depth(void)
{
    if (s_queue == NULL) {
        return 0;
    }

    return (size_t)uxQueueMessagesWaiting(s_queue);
}
