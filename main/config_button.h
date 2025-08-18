#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*config_button_callback_t)(uint32_t press_count, uint32_t press_duration);

// Initialize config button on GPIO34
esp_err_t config_button_init(config_button_callback_t callback);

uint32_t config_button_get_press_count(void);
void config_button_reset_counter(void);

// Set or update the button press callback. Can be NULL to disable
void config_button_set_callback(config_button_callback_t callback);
