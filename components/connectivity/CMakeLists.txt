idf_component_register(
    SRCS
        "ethernet_service.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_eth esp_event esp_netif
)
