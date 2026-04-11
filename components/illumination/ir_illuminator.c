#include "ir_illuminator.h"

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "runtime_config.h"

/* One GPIO-controlled IR output with an optional future coupling to the capture lifecycle. */
static const char *TAG = "ir_illuminator";

typedef struct {
    bool initialized;
    bool capture_active;
    bool output_on;
} ir_illuminator_state_t;

static ir_illuminator_state_t s_state;

/* Translate the logical ON/OFF state into the actual GPIO level expected by the hardware. */
static int ir_illuminator_output_level(bool on)
{
    if (APP_IR_ILLUMINATOR_ACTIVE_HIGH) {
        return on ? 1 : 0;
    }

    return on ? 0 : 1;
}

/* Resolve one explicit mode into the output state that should be driven right now. */
static bool ir_illuminator_should_be_on_for_mode(ir_illuminator_mode_t mode)
{
    switch (mode) {
    case IR_ILLUMINATOR_MODE_OFF:
        return false;
    case IR_ILLUMINATOR_MODE_ON:
        return true;
    case IR_ILLUMINATOR_MODE_CAPTURE:
        return s_state.capture_active;
    default:
        return false;
    }
}

/* Drive the GPIO and remember the resulting logical output state for diagnostics. */
static esp_err_t ir_illuminator_drive_output(bool on)
{
    ESP_RETURN_ON_ERROR(gpio_set_level((gpio_num_t)APP_IR_ILLUMINATOR_GPIO, ir_illuminator_output_level(on)),
                        TAG, "failed to drive IR illuminator GPIO");

    s_state.output_on = on;
    return ESP_OK;
}

/* Recompute the output from one explicit mode and optional capture state. */
static esp_err_t ir_illuminator_apply_output(ir_illuminator_mode_t mode, const char *reason)
{
    bool desired_on;

    if (!APP_HAS_IR_ILLUMINATOR) {
        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(s_state.initialized, ESP_ERR_INVALID_STATE, TAG, "IR illuminator not initialized");

    desired_on = ir_illuminator_should_be_on_for_mode(mode);

    ESP_RETURN_ON_ERROR(ir_illuminator_drive_output(desired_on), TAG, "failed to update IR illuminator output");

    ESP_LOGI(TAG,
             "IR illuminator updated: mode=%s, capture_active=%s, output=%s (%s)",
             runtime_config_ir_illuminator_mode_to_string(mode),
             s_state.capture_active ? "true" : "false",
             desired_on ? "on" : "off",
             reason);
    return ESP_OK;
}

/* Configure the GPIO as a push/pull output and leave the illuminator off until runtime config is applied. */
esp_err_t ir_illuminator_init(void)
{
    if (!APP_HAS_IR_ILLUMINATOR) {
        return ESP_OK;
    }

    if (s_state.initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(APP_IR_ILLUMINATOR_GPIO),
                        ESP_ERR_INVALID_ARG, TAG, "invalid IR illuminator GPIO");

    s_state.capture_active = false;
    s_state.output_on = false;

    ESP_RETURN_ON_ERROR(gpio_reset_pin((gpio_num_t)APP_IR_ILLUMINATOR_GPIO), TAG, "failed to reset IR GPIO");
    ESP_RETURN_ON_ERROR(gpio_set_direction((gpio_num_t)APP_IR_ILLUMINATOR_GPIO, GPIO_MODE_OUTPUT),
                        TAG, "failed to set IR GPIO as output");

    /*
     * Force a safe OFF state before any runtime policy is applied.
     * This avoids a brief unintended flash during boot on boards where the line state is undefined after reset.
     */
    ESP_RETURN_ON_ERROR(ir_illuminator_drive_output(false), TAG, "failed to set safe IR default state");

    s_state.initialized = true;
    ESP_LOGI(TAG, "IR illuminator initialized on GPIO %d", APP_IR_ILLUMINATOR_GPIO);
    return ESP_OK;
}

/* Apply the persisted runtime mode to the already configured output. */
esp_err_t ir_illuminator_apply_runtime_config(void)
{
    return ir_illuminator_apply_output(runtime_config_get_ir_illuminator_mode(), "runtime config");
}

/* Apply one explicit runtime mode to the live output without reading runtime_config. */
esp_err_t ir_illuminator_apply_mode(ir_illuminator_mode_t mode)
{
    if (mode != IR_ILLUMINATOR_MODE_OFF
        && mode != IR_ILLUMINATOR_MODE_ON
        && mode != IR_ILLUMINATOR_MODE_CAPTURE) {
        return ESP_ERR_INVALID_ARG;
    }

    return ir_illuminator_apply_output(mode, "explicit mode");
}

/* Let future camera code signal the beginning and end of an actual capture session. */
esp_err_t ir_illuminator_set_capture_active(bool capture_active)
{
    if (!APP_HAS_IR_ILLUMINATOR) {
        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(s_state.initialized, ESP_ERR_INVALID_STATE, TAG, "IR illuminator not initialized");

    s_state.capture_active = capture_active;

    /*
     * Only CAPTURE mode reacts to this flag today, but recomputing the full output state here keeps
     * the behavior centralized and avoids duplicating mode checks in future camera code.
     */
    return ir_illuminator_apply_output(runtime_config_get_ir_illuminator_mode(),
                                       capture_active ? "capture started" : "capture stopped");
}

/* Report whether IR illuminator support is compiled into this firmware build. */
bool ir_illuminator_is_supported(void)
{
    return APP_HAS_IR_ILLUMINATOR;
}

/* Report the last logical ON/OFF state driven on the illuminator output. */
bool ir_illuminator_is_on(void)
{
    return APP_HAS_IR_ILLUMINATOR ? s_state.output_on : false;
}
