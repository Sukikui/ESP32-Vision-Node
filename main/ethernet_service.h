#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t ethernet_service_init(void);
esp_err_t ethernet_service_start(void);
esp_err_t ethernet_service_wait_for_ip(TickType_t timeout_ticks);
bool ethernet_service_is_up(void);
bool ethernet_service_is_link_up(void);
esp_err_t ethernet_service_get_ipv4_string(char *buffer, size_t buffer_len);
esp_err_t ethernet_service_get_gateway_string(char *buffer, size_t buffer_len);
