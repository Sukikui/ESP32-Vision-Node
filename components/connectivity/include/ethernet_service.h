#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

/* Initialize the Ethernet driver, netif, glue layer, and event handlers. */
esp_err_t ethernet_service_init(void);

/* Start the Ethernet interface after initialization. */
esp_err_t ethernet_service_start(void);

/* Block until an IPv4 address is acquired or the timeout expires. */
esp_err_t ethernet_service_wait_for_ip(TickType_t timeout_ticks);

/* Format the current IPv4 address as dotted decimal text. */
esp_err_t ethernet_service_get_ipv4_string(char *buffer, size_t buffer_len);

/* Format the current default gateway as dotted decimal text. */
esp_err_t ethernet_service_get_gateway_string(char *buffer, size_t buffer_len);
