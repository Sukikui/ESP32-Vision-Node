#include "publish_queue.h"

#include <stdint.h>
#include <string.h>

#include "app_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mqtt_service.h"

/* One MQTT publication request copied into RAM so it can outlive the caller's local buffers. */
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

/* Dequeue one item at a time and keep retrying it until esp-mqtt accepts it. */
static void publish_queue_task(void *arg)
{
    publish_queue_item_t item;

    while (true) {
        if (xQueueReceive(s_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        /*
         * The queue is the only buffering layer between callers and the broker.
         * Once an item is in the queue, this task keeps it alive until the client reconnects and accepts it.
         */
        while (mqtt_service_publish_immediate_bytes(item.topic, item.data, item.data_len, item.qos, item.retain) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }
}

/* Allocate the fixed-size FreeRTOS queue that stores outgoing MQTT publications. */
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

/* Start the worker task that drains queued publications. */
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

/* Copy one publication request into the fixed-size RAM queue. */
esp_err_t publish_queue_push(const char *topic, const void *data, size_t data_len, int qos, bool retain)
{
    publish_queue_item_t item = {0};

    if (topic == NULL || (data == NULL && data_len > 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (data_len > sizeof(item.data)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (strlcpy(item.topic, topic, sizeof(item.topic)) >= sizeof(item.topic)) {
        return ESP_ERR_INVALID_SIZE;
    }

    item.data_len = data_len;
    item.qos = qos;
    item.retain = retain;

    /* Copy payload bytes into the queue item so callers can reuse or free their source buffer immediately. */
    if (data != NULL && data_len > 0) {
        memcpy(item.data, data, data_len);
    }

    if (xQueueSend(s_queue, &item, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGW(TAG, "publish queue full for topic %s", topic);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

/* Return the current queued message count for diagnostics. */
size_t publish_queue_get_depth(void)
{
    if (s_queue == NULL) {
        return 0;
    }

    return (size_t)uxQueueMessagesWaiting(s_queue);
}
