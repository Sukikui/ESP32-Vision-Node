#include <stdio.h>

#include "app_config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "ethernet_service.h"
#include "heartbeat_task.h"
#include "mqtt_service.h"
#include "node_event.h"
#include "publish_queue.h"
#include "topic_map.h"

static const char *TAG = "app_main";

void app_main(void)
{
    esp_err_t err;

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(topic_map_init(APP_NODE_ID));
    ESP_ERROR_CHECK(ethernet_service_init());
    ESP_ERROR_CHECK(publish_queue_init());
    ESP_ERROR_CHECK(publish_queue_start());
    ESP_ERROR_CHECK(ethernet_service_start());
    ESP_ERROR_CHECK(ethernet_service_wait_for_ip(portMAX_DELAY));

    ESP_ERROR_CHECK(mqtt_service_init());
    ESP_ERROR_CHECK(mqtt_service_start());
    ESP_ERROR_CHECK(heartbeat_task_start());
    ESP_ERROR_CHECK(node_event_publish("boot_completed"));

    ESP_LOGI(TAG, "Ethernet + MQTT control plane started for node %s", APP_NODE_ID);
}
