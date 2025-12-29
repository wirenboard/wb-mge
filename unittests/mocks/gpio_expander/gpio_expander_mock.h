#pragma once

#include "gpio_expander.h"

#define MOCK_GPIO_EXPANDER_MAX_FUNC_CALLS   20
#define MOCK_GPIO_EXPANDER_INIT_HANDLE      (esp_io_expander_handle_t)0xBAADFACE

typedef struct {
    int called;
    unsigned call_seq;
    bool should_fail;
} mock_gpio_expander_init_t;

typedef struct {
    int called;
    unsigned call_seq[MOCK_GPIO_EXPANDER_MAX_FUNC_CALLS];
    uint32_t masks[MOCK_GPIO_EXPANDER_MAX_FUNC_CALLS];
    esp_io_expander_dir_t directions[MOCK_GPIO_EXPANDER_MAX_FUNC_CALLS];
    bool should_fail;
} mock_gpio_expander_set_dir_t;

typedef struct {
    int called;
    unsigned call_seq[MOCK_GPIO_EXPANDER_MAX_FUNC_CALLS];
    uint32_t masks[MOCK_GPIO_EXPANDER_MAX_FUNC_CALLS];
    uint8_t levels[MOCK_GPIO_EXPANDER_MAX_FUNC_CALLS];
    bool should_fail;
} mock_gpio_expander_set_level_t;

typedef struct {
    int called;
    unsigned call_seq[MOCK_GPIO_EXPANDER_MAX_FUNC_CALLS];
    uint32_t masks[MOCK_GPIO_EXPANDER_MAX_FUNC_CALLS];
    uint8_t levels[MOCK_GPIO_EXPANDER_MAX_FUNC_CALLS];
    bool should_fail;
} mock_gpio_expander_set_out_dir_and_level_t;

typedef struct {
    int called;
    unsigned call_seq[MOCK_GPIO_EXPANDER_MAX_FUNC_CALLS];
    uint32_t masks[MOCK_GPIO_EXPANDER_MAX_FUNC_CALLS];
    uint32_t level_setup;
    bool should_fail;
} mock_gpio_expander_get_level_t;

extern mock_gpio_expander_init_t mock_gpio_expander_init_data;
extern mock_gpio_expander_set_dir_t mock_gpio_expander_set_dir_data;
extern mock_gpio_expander_set_level_t mock_gpio_expander_set_level_data;
extern mock_gpio_expander_set_out_dir_and_level_t mock_gpio_expander_set_out_dir_and_level_data;
extern mock_gpio_expander_get_level_t mock_gpio_expander_get_level_data;

void mock_gpio_expander_reset(void);
