#pragma once

#include "esp_err.h"

typedef enum {
    IO_EXPANDER_INPUT,          /*!< Input direction */
    IO_EXPANDER_OUTPUT,         /*!< Output dircetion */
} esp_io_expander_dir_t;

typedef enum {
    IO_EXPANDER_PIN_NUM_8  = (1ULL << 8),
} esp_io_expander_pin_num_t;

typedef void *esp_io_expander_handle_t;

esp_err_t esp_io_expander_print_state(esp_io_expander_handle_t handle);
esp_err_t esp_io_expander_set_dir(esp_io_expander_handle_t handle, uint32_t pin_num_mask, esp_io_expander_dir_t direction);
esp_err_t esp_io_expander_set_level(esp_io_expander_handle_t handle, uint32_t pin_num_mask, uint8_t level);
