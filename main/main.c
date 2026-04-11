#include <inttypes.h>
#include <stdio.h>

#include "app_config.h"
#include "motion_detection.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "ethernet_service.h"
#include "heartbeat_task.h"
#include "ir_illuminator.h"
#include "mqtt_service.h"
#include "node_event.h"
#include "publish_queue.h"
#include "runtime_config.h"
#include "topic_map.h"


/**
 *      ___________ ____ ________      _    ___      _                   _   __          __   
 *     / ____/ ___// __ \__  /__ \    | |  / (_)____(_)___  ____        / | / /___  ____/ /__ 
 *    / __/  \__ \/ /_/ //_ <__/ /____| | / / / ___/ / __ \/ __ \______/  |/ / __ \/ __  / _ \
 *   / /___ ___/ / ____/__/ / __/_____/ |/ / (__  ) / /_/ / / / /_____/ /|  / /_/ / /_/ /  __/
 *  /_____//____/_/   /____/____/     |___/_/____/_/\____/_/ /_/     /_/ |_/\____/\__,_/\___/ 
 *
 * --------------------------------------------------------------------------------------------
 * Developped by @Sukikui
 * Repository: github.com/Sukikui/ESP32-Vision-Node
 * Copyright (c) 2026. Licensed under the MIT License.
 * --------------------------------------------------------------------------------------------
 */


/* Main firmware entry point and high-level service wiring. */
static const char *TAG = "app_main";

/* Translate internal PIR events into MQTT node events without coupling detection to MQTT. */
static void motion_detection_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    const int64_t *timestamp_us = (const int64_t *)event_data;

    if (event_id != MOTION_DETECTION_EVENT_TRIGGERED || timestamp_us == NULL) {
        return;
    }

    ESP_LOGI(TAG, "Motion detected at %" PRIi64 " us", *timestamp_us);

    if (node_event_publish("motion_detected") != ESP_OK) {
        ESP_LOGW(TAG, "failed to publish motion_detected event");
    }
}

/* Bootstrap the runtime and start all services in dependency order. */
void app_main(void)
{
    esp_err_t err;

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* Load persisted runtime overrides before starting services that consume them. */
    ESP_ERROR_CHECK(runtime_config_init());
    ESP_ERROR_CHECK(ir_illuminator_init());
    /* Apply the persisted IR policy early so an always-on illuminator mode takes effect before network startup. */
    ESP_ERROR_CHECK(ir_illuminator_apply_runtime_config());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    if (motion_detection_is_supported()) {
        /* The detector posts on the default event loop, so the bridge handler must be registered early. */
        ESP_ERROR_CHECK(esp_event_handler_register(
            MOTION_DETECTION_EVENT,
            MOTION_DETECTION_EVENT_TRIGGERED,
            motion_detection_event_handler,
            NULL));
    }
    ESP_ERROR_CHECK(topic_map_init(APP_NODE_ID));
    ESP_ERROR_CHECK(ethernet_service_init());
    ESP_ERROR_CHECK(publish_queue_init());
    ESP_ERROR_CHECK(publish_queue_start());
    ESP_ERROR_CHECK(motion_detection_init());

    /* MQTT discovery depends on DHCP gateway information, so Ethernet must be up first. */
    ESP_ERROR_CHECK(ethernet_service_start());
    ESP_ERROR_CHECK(ethernet_service_wait_for_ip(portMAX_DELAY));

    ESP_ERROR_CHECK(mqtt_service_init());
    ESP_ERROR_CHECK(mqtt_service_start());

    /* Apply the restored heartbeat interval before the task starts using it in its delay loop. */
    ESP_ERROR_CHECK(heartbeat_task_set_interval_s(runtime_config_get_heartbeat_interval_s()));

    /* Start optional services only after the control plane is ready to publish their events. */
    ESP_ERROR_CHECK(motion_detection_start());
    ESP_ERROR_CHECK(heartbeat_task_start());
    ESP_ERROR_CHECK(node_event_publish("boot_completed"));

    ESP_LOGI(TAG, "Ethernet + MQTT control plane started for node %s", APP_NODE_ID);
}
