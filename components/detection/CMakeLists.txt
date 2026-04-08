idf_component_register(
    SRCS
        "motion_detection.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES app_config esp_driver_gpio esp_event esp_timer
)
