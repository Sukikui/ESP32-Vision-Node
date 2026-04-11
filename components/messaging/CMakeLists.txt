idf_component_register(
    SRCS
        "command_router.c"
        "heartbeat_task.c"
        "image_transfer.c"
        "mqtt_service.c"
        "node_event.c"
        "publish_queue.c"
        "topic_map.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES app_config connectivity detection esp_event esp_timer illumination runtime_config
)
