#include "motion_detection.h"

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "runtime_config.h"

ESP_EVENT_DEFINE_BASE(MOTION_DETECTION_EVENT);

/* PIR detection built around one GPIO interrupt and one worker task. */
static const char *TAG = "motion_detection";

typedef struct {
    TaskHandle_t task_handle;
    bool initialized;
    bool started;
    uint32_t warmup_ms;
    uint32_t cooldown_ms;
    int64_t warmup_until_us;
    int64_t last_trigger_us;
} motion_detection_state_t;

static motion_detection_state_t s_state;

/* IRQ handler: only wake the worker task and leave all timing logic outside interrupt context. */
static void IRAM_ATTR motion_detection_isr_handler(void *arg)
{
    BaseType_t task_woken = pdFALSE;

    if (s_state.task_handle != NULL) {
        vTaskNotifyGiveFromISR(s_state.task_handle, &task_woken);
        portYIELD_FROM_ISR(task_woken);
    }
}

/* Post one accepted trigger timestamp onto the default ESP event loop. */
static esp_err_t motion_detection_publish_trigger(int64_t timestamp_us)
{
    return esp_event_post(
        MOTION_DETECTION_EVENT,
        MOTION_DETECTION_EVENT_TRIGGERED,
        &timestamp_us,
        sizeof(timestamp_us),
        pdMS_TO_TICKS(100));
}

/* Apply one explicit runtime state and decide whether the detector should be armed at all. */
static esp_err_t motion_detection_apply_settings_internal(bool should_enable, uint32_t warmup_ms, uint32_t cooldown_ms)
{
    bool was_started;

    if (!APP_HAS_MOTION_DETECTION) {
        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(s_state.initialized, ESP_ERR_INVALID_STATE, TAG, "motion detection not initialized");

    was_started = s_state.started;
    s_state.warmup_ms = warmup_ms;
    s_state.cooldown_ms = cooldown_ms;

    if (!should_enable) {
        s_state.started = false;
        s_state.warmup_until_us = 0;
        s_state.last_trigger_us = 0;

        if (was_started) {
            ESP_LOGI(TAG, "PIR motion detection disabled by runtime config");
        }
        return ESP_OK;
    }

    if (!was_started) {
        /*
         * Re-arming starts a fresh warm-up window because the PIR may have been idle or intentionally disabled.
         * This keeps the first trigger after re-enable consistent with the normal boot path.
         */
        s_state.warmup_until_us = esp_timer_get_time() + ((int64_t)s_state.warmup_ms * 1000LL);
        s_state.last_trigger_us = 0;
        s_state.started = true;

        ESP_LOGI(TAG,
                 "PIR motion detection started: active_%s, warmup=%" PRIu32 " ms, cooldown=%" PRIu32 " ms",
                 APP_MOTION_PIR_ACTIVE_HIGH ? "high" : "low",
                 s_state.warmup_ms,
                 s_state.cooldown_ms);
        return ESP_OK;
    }

    /*
     * When the detector is already running, only the timing parameters change live.
     * The current warm-up window and last accepted trigger stay untouched to avoid surprising resets mid-run.
     */
    ESP_LOGI(TAG,
             "PIR motion detection updated: warmup=%" PRIu32 " ms, cooldown=%" PRIu32 " ms",
             s_state.warmup_ms,
             s_state.cooldown_ms);
    return ESP_OK;
}

/* Consume raw PIR edges and filter them through warm-up and cooldown rules. */
static void motion_detection_task(void *arg)
{
    while (true) {
        int64_t cooldown_us;

        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (!s_state.started) {
            continue;
        }

        int64_t now_us = esp_timer_get_time();
        /* Ignore edges while the PIR is still stabilizing after startup. */
        if (now_us < s_state.warmup_until_us) {
            ESP_LOGD(TAG, "ignoring PIR trigger during warm-up");
            continue;
        }

        /* Convert the runtime cooldown from milliseconds to microseconds right before applying it. */
        cooldown_us = (int64_t)s_state.cooldown_ms * 1000LL;

        /* Ignore edges that arrive too soon after the previous accepted detection. */
        if (cooldown_us > 0 && s_state.last_trigger_us > 0 && (now_us - s_state.last_trigger_us) < cooldown_us) {
            ESP_LOGD(TAG, "ignoring PIR trigger during cooldown");
            continue;
        }

        s_state.last_trigger_us = now_us;
        ESP_LOGI(TAG, "motion detected on GPIO %d", APP_MOTION_PIR_GPIO);

        if (motion_detection_publish_trigger(now_us) != ESP_OK) {
            ESP_LOGW(TAG, "failed to publish motion event to default event loop");
        }
    }
}

/* Configure the PIR GPIO, hook its ISR, and create the worker task that evaluates triggers. */
esp_err_t motion_detection_init(void)
{
    gpio_config_t io_config = {0};
    gpio_int_type_t intr_type;
    esp_err_t err;

    if (!APP_HAS_MOTION_DETECTION) {
        return ESP_OK;
    }

    if (s_state.initialized) {
        return ESP_OK;
    }

    if (!GPIO_IS_VALID_GPIO(APP_MOTION_PIR_GPIO)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s_state, 0, sizeof(s_state));
    s_state.warmup_ms = APP_MOTION_DEFAULT_WARMUP_MS;
    s_state.cooldown_ms = APP_MOTION_DEFAULT_COOLDOWN_MS;

    intr_type = APP_MOTION_PIR_ACTIVE_HIGH ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE;
    io_config.pin_bit_mask = 1ULL << APP_MOTION_PIR_GPIO;
    io_config.mode = GPIO_MODE_INPUT;
    io_config.pull_up_en = GPIO_PULLUP_DISABLE;
    io_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_config.intr_type = intr_type;

    ESP_RETURN_ON_ERROR(gpio_config(&io_config), TAG, "failed to configure PIR GPIO");

    /* The GPIO ISR service is global in ESP-IDF, so "already installed" is not an error here. */
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    ESP_RETURN_ON_ERROR(gpio_isr_handler_add((gpio_num_t)APP_MOTION_PIR_GPIO, motion_detection_isr_handler, NULL),
                        TAG, "failed to add PIR ISR handler");

    if (xTaskCreate(motion_detection_task, "motion_detection", 4096, NULL, 5, &s_state.task_handle) != pdPASS) {
        s_state.task_handle = NULL;
        return ESP_FAIL;
    }

    s_state.initialized = true;
    ESP_LOGI(TAG, "PIR motion detection initialized on GPIO %d", APP_MOTION_PIR_GPIO);
    return ESP_OK;
}

/* Arm the detector and remember when the warm-up window will end. */
esp_err_t motion_detection_start(void)
{
    return motion_detection_apply_settings_internal(runtime_config_get_motion_detection_enabled(),
                                                    runtime_config_get_motion_warmup_ms(),
                                                    runtime_config_get_motion_cooldown_ms());
}

/* Apply one explicit runtime state to the live detector without reading runtime_config. */
esp_err_t motion_detection_apply_settings(bool enabled, uint32_t warmup_ms, uint32_t cooldown_ms)
{
    return motion_detection_apply_settings_internal(enabled, warmup_ms, cooldown_ms);
}

/* Report whether PIR support is enabled at build time. */
bool motion_detection_is_supported(void)
{
    return APP_HAS_MOTION_DETECTION;
}

/* Report whether PIR detection has finished warm-up and can emit events. */
bool motion_detection_is_armed(void)
{
    if (!APP_HAS_MOTION_DETECTION || !s_state.started) {
        return false;
    }

    return esp_timer_get_time() >= s_state.warmup_until_us;
}

/* Return the timestamp of the last accepted trigger for diagnostics. */
int64_t motion_detection_get_last_trigger_us(void)
{
    return s_state.last_trigger_us;
}
