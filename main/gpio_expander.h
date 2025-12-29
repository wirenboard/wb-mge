#pragma once

#include "esp_err.h"
#include "esp_io_expander.h"

// This module implements thread-safe operations with GPIO expander

// Initialize GPIO expander
// handle - output expander handle pointer, can be NULL if not needed
esp_err_t gpio_expander_init(esp_io_expander_handle_t* handle);

esp_err_t gpio_expander_set_dir(uint32_t pin_num_mask, esp_io_expander_dir_t direction);
esp_err_t gpio_expander_set_level(uint32_t pin_num_mask, uint8_t level);
esp_err_t gpio_expander_set_out_dir_and_level(uint32_t pin_num_mask, uint8_t level);
esp_err_t gpio_expander_get_level(uint32_t pin_num_mask, uint32_t *level_mask);
