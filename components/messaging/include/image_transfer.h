#pragma once

#include <stddef.h>

#include "esp_err.h"

/* Publish one image as meta + chunk stream + done marker over MQTT. */
esp_err_t image_transfer_publish(const char *capture_id, const void *data, size_t data_len, const char *content_type);
