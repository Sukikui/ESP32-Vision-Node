idf_component_register(
    SRCS
        "runtime_config.c"
    INCLUDE_DIRS "include"
    REQUIRES app_config
    PRIV_REQUIRES nvs_flash
)
