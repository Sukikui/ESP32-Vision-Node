idf_component_register(
    SRCS
        "main.c"
    INCLUDE_DIRS "."
    REQUIRES app_config connectivity detection illumination messaging runtime_config
    PRIV_REQUIRES esp_event esp_netif nvs_flash
)
