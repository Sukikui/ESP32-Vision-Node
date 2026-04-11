#include "json_utils.h"

/* Render one cJSON object directly into a caller-provided buffer. */
esp_err_t json_utils_print_to_buffer(cJSON *root, char *buffer, size_t buffer_len)
{
    if (root == NULL || buffer == NULL || buffer_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * cJSON can print directly into a caller-owned buffer.
     * This avoids the temporary heap string allocated by cJSON_PrintUnformatted().
     */
    buffer[0] = '\0';
    if (!cJSON_PrintPreallocated(root, buffer, (int)buffer_len, false)) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}
