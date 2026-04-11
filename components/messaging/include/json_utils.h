#pragma once

#include <stddef.h>

#include "cJSON.h"
#include "esp_err.h"

/* Render one cJSON object directly into a caller-provided buffer. */
esp_err_t json_utils_print_to_buffer(cJSON *root, char *buffer, size_t buffer_len);
