#include "config_button.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

//------------------------------------------------------------------------------

#define CONFIG_BUTTON_GPIO                  GPIO_NUM_34
#define CONFIG_BUTTON_DEBOUNCE_TIME_MS      50

#define CONFIG_BUTTON_TASK_STACK_SIZE       3072
#define CONFIG_BUTTON_TASK_PRIORITY         2

//------------------------------------------------------------------------------

typedef enum {
    BTN_STATE_RELEASE = 0,
    BTN_STATE_PRE_ACT,
    BTN_STATE_ACTIVE,
    BTN_STATE_PRE_REL
} btn_state_t;

typedef struct {
    bool initialized;
    unsigned press_counter;
    config_button_press_callback_t press_callback;
    config_button_longpress_callback_t long_press_callback;
    unsigned long_press_time;
    bool bool_state;
} config_btn_ctx_t;

//------------------------------------------------------------------------------

static const char* TAG = "config_button";

static config_btn_ctx_t config_btn_ctx = {0};

//------------------------------------------------------------------------------

static bool debounce_filter(btn_state_t* state, unsigned* time_stamp, const unsigned sys_time, const bool is_pressed)
{
    switch (*state) {
        default: {
            *state = BTN_STATE_RELEASE;
            __attribute__((fallthrough));
        }
        case BTN_STATE_RELEASE: {
            if (is_pressed) {
                *time_stamp = sys_time;
                *state = BTN_STATE_PRE_ACT;
            }
            break;
        }
        case BTN_STATE_PRE_ACT: {
            if (!is_pressed) {
                *state = BTN_STATE_RELEASE;
                break;
            }
            if ((sys_time - *time_stamp) >= CONFIG_BUTTON_DEBOUNCE_TIME_MS) {
                *state = BTN_STATE_ACTIVE;
            }
            break;
        }
        case BTN_STATE_ACTIVE: {
            if (!is_pressed) {
                *time_stamp = sys_time;
                *state = BTN_STATE_PRE_REL;
            }
            break;
        }
        case BTN_STATE_PRE_REL: {
            if (is_pressed) {
                *state = BTN_STATE_ACTIVE;
                break;
            }
            if ((sys_time - *time_stamp) >= CONFIG_BUTTON_DEBOUNCE_TIME_MS) {
                *state = BTN_STATE_RELEASE;
            }
            break;
        }
    }

    bool pressed = (*state == BTN_STATE_ACTIVE) || (*state == BTN_STATE_PRE_REL);
    return pressed;
}

//------------------------------------------------------------------------------

static void config_button_task(void *arg)
{
    btn_state_t state = BTN_STATE_RELEASE;
    unsigned time_stamp = pdTICKS_TO_MS(xTaskGetTickCount());
    unsigned long_press_time_stamp = time_stamp;
    bool pending_long_press = 0;

    while (1)
    {
        unsigned sys_time = pdTICKS_TO_MS(xTaskGetTickCount());
        bool pressed = !gpio_get_level(CONFIG_BUTTON_GPIO);

        bool old_bool_state = config_btn_ctx.bool_state;
        config_btn_ctx.bool_state = debounce_filter(&state, &time_stamp, sys_time, pressed);

        if (config_btn_ctx.bool_state && (config_btn_ctx.bool_state != old_bool_state)) { // Press event
            config_btn_ctx.press_counter++;
            long_press_time_stamp = sys_time;
            pending_long_press = 1;
            ESP_LOGI(TAG, "Button press event, counter: %u", config_btn_ctx.press_counter);
            if (config_btn_ctx.press_callback) {
                config_btn_ctx.press_callback(config_btn_ctx.press_counter);
            }
        } else if (!config_btn_ctx.bool_state) {
            pending_long_press = 0;
        }

        unsigned hold_time = sys_time - long_press_time_stamp;
        if (pending_long_press && (hold_time >= config_btn_ctx.long_press_time)) { // Long press event
            ESP_LOGI(TAG, "Button long press event, hold time: %u ms", hold_time);
            if (config_btn_ctx.long_press_callback) {
                config_btn_ctx.long_press_callback(hold_time);
            }
            pending_long_press = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

//------------------------------------------------------------------------------

esp_err_t config_button_init(void)
{
    if (config_btn_ctx.initialized) {
        return ESP_OK;  // Already initialized
    }

    // Configure GPIO34 as input with pull-up
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,          // GPIO34 has no build-in pull-up, using external pull-up resistor
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to initialize GPIO");
        return ret;
    }

    BaseType_t result = xTaskCreate(config_button_task, "config_button_task", CONFIG_BUTTON_TASK_STACK_SIZE, NULL, CONFIG_BUTTON_TASK_PRIORITY, NULL);
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        return ESP_FAIL;
    }

    config_btn_ctx.initialized = 1;
    ESP_LOGI(TAG, "Button initialized");

    return ESP_OK;
}

//------------------------------------------------------------------------------

void config_button_set_press_callback(config_button_press_callback_t callback)
{
    config_btn_ctx.press_callback = callback;
}

void config_button_set_longpress_callback(config_button_longpress_callback_t callback, unsigned hold_time_ms)
{
    config_btn_ctx.long_press_time = hold_time_ms;
    config_btn_ctx.long_press_callback = callback;
}

//------------------------------------------------------------------------------

bool config_button_is_pressed(void)
{
    return config_btn_ctx.bool_state;
}

//------------------------------------------------------------------------------

unsigned config_button_get_press_count(void)
{
    return config_btn_ctx.press_counter;
}


void config_button_reset_counter(void)
{
    config_btn_ctx.press_counter = 0;
}

//------------------------------------------------------------------------------
