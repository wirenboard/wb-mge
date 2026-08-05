#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "hal/gpio_types.h"
typedef struct {
    uint64_t pin_bit_mask;
    gpio_mode_t mode;
    int pull_up_en;
    int pull_down_en;
    int intr_type;
} gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *cfg);
esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode);
esp_err_t gpio_reset_pin(gpio_num_t gpio_num);
esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level);
int gpio_get_level(gpio_num_t gpio_num);
