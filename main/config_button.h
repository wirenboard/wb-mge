#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef void (*config_button_press_callback_t)(unsigned press_counter);
typedef void (*config_button_longpress_callback_t)(unsigned press_time_ms);

// Initialize config button on GPIO34
esp_err_t config_button_init(void);

// Set or update the button single press callback. Can be NULL to disable
void config_button_set_press_callback(config_button_press_callback_t callback);

// Set or update the button long press callback. Can be NULL to disable
// Provide hold_time_ms to set long press duration
void config_button_set_longpress_callback(config_button_longpress_callback_t callback, unsigned hold_time_ms);

// Get button press counter
unsigned config_button_get_press_count(void);

// Reset button press counter
void config_button_reset_counter(void);
