idf_component_register(
    SRCS
        "ir_illuminator.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES app_config esp_driver_gpio runtime_config
)
