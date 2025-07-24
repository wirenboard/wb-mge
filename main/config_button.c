#include "config_button.h"

#include "esp_err.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define CONFIG_BUTTON_GPIO          GPIO_NUM_34
#define BUTTON_DEBOUNCE_TIME_MS     50
#define BUTTON_LONG_PRESS_TIME_MS   1000

static volatile uint32_t button_press_count = 0;
static config_button_callback_t button_callback = NULL;
static volatile bool button_initialized = false;
static volatile uint64_t button_press_start_time = 0;
static volatile bool button_pressed = false;

static void IRAM_ATTR config_button_isr_handler(void* arg)
{
    static uint64_t last_interrupt_time = 0;
    uint64_t current_time = esp_timer_get_time();

    // Simple debouncing - ignore interrupts within debounce time
    if (current_time - last_interrupt_time < (BUTTON_DEBOUNCE_TIME_MS * 1000)) {
        return;
    }
    last_interrupt_time = current_time;

    int button_state = gpio_get_level(CONFIG_BUTTON_GPIO);

    if (button_state == 0 && !button_pressed) {
        // Button pressed (falling edge)
        button_pressed = true;
        button_press_start_time = current_time;
    } else if (button_state == 1 && button_pressed) {
        // Button released (rising edge)
        button_pressed = false;
        button_press_count++;

        // Calculate press duration
        uint32_t press_duration = (current_time - button_press_start_time) / 1000;

        // Call callback if set (from ISR, so keep it simple)
        if (button_callback != NULL) {
            button_callback(button_press_count, press_duration);
        }
    }
}

esp_err_t config_button_init(config_button_callback_t callback)
{
    if (button_initialized) {
        return ESP_OK;  // Already initialized
    }

    // Configure GPIO34 as input with pull-up
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE  // Trigger on both edges
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        return ret;
    }

    // Install GPIO ISR service if not already installed
    ret = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE means ISR service already installed
        return ret;
    }

    // Add ISR handler for this GPIO
    ret = gpio_isr_handler_add(CONFIG_BUTTON_GPIO, config_button_isr_handler, NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    button_callback = callback;
    button_press_count = 0;
    button_pressed = false;
    button_press_start_time = 0;
    button_initialized = true;

    return ESP_OK;
}

uint32_t config_button_get_press_count(void)
{
    return button_press_count;
}

void config_button_reset_counter(void)
{
    button_press_count = 0;
}

void config_button_set_callback(config_button_callback_t callback)
{
    button_callback = callback;
}
