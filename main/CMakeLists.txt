idf_component_register(
    SRCS
        "main.c"
        "node_event.c"
        "ethernet_service.c"
        "mqtt_service.c"
        "topic_map.c"
        "command_router.c"
        "heartbeat_task.c"
        "publish_queue.c"
        "image_transfer.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_eth esp_event esp_netif nvs_flash esp_timer
)
