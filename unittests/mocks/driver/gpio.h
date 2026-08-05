#pragma once

#include "esp_err.h"
#include <stdint.h>

typedef enum {
    GPIO_NUM_0 = 0,
    GPIO_NUM_1 = 1,
    GPIO_NUM_4 = 4,
    GPIO_NUM_9 = 9,
    GPIO_NUM_10 = 10,
    GPIO_NUM_12 = 12,
    GPIO_NUM_14 = 14,
    GPIO_NUM_15 = 15,
} gpio_num_t;

typedef enum {
    GPIO_MODE_INPUT = 0,
    GPIO_MODE_OUTPUT,
    GPIO_MODE_OUTPUT_OD,
    GPIO_MODE_INPUT_OUTPUT_OD,
    GPIO_MODE_INPUT_OUTPUT,
} gpio_mode_t;

esp_err_t gpio_reset_pin(gpio_num_t gpio_num);
esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode);
esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level);

typedef struct {
    int called;
    gpio_num_t gpio_num;
    unsigned call_seq;
} mock_gpio_reset_pin_t;

typedef struct {
    int called;
    gpio_num_t gpio_num;
    gpio_mode_t mode;
    unsigned call_seq;
} mock_gpio_set_direction_t;

typedef struct {
    int called;
    gpio_num_t gpio_num;
    uint32_t level;
    unsigned call_seq;
} mock_gpio_set_level_t;

extern mock_gpio_reset_pin_t     mock_gpio_reset_pin_data;
extern mock_gpio_set_direction_t mock_gpio_set_direction_data;
extern mock_gpio_set_level_t     mock_gpio_set_level_data;

void mock_gpio_reset(void);
