#pragma once

#include "config.h"
#include "esp_err.h"

#define MOCK_IO_EXPANDER_HANDLE             ((esp_io_expander_handle_t)0xABCD1234)

typedef enum {
    IO_EXPANDER_INPUT,          /*!< Input direction */
    IO_EXPANDER_OUTPUT,         /*!< Output direction */
} esp_io_expander_dir_t;

typedef enum {
    IO_EXPANDER_PIN_NUM_0  = (1ULL << 0),
    IO_EXPANDER_PIN_NUM_1  = (1ULL << 1),
    IO_EXPANDER_PIN_NUM_2  = (1ULL << 2),
    IO_EXPANDER_PIN_NUM_3  = (1ULL << 3),
    IO_EXPANDER_PIN_NUM_4  = (1ULL << 4),
    IO_EXPANDER_PIN_NUM_5  = (1ULL << 5),
    IO_EXPANDER_PIN_NUM_6  = (1ULL << 6),
    IO_EXPANDER_PIN_NUM_7  = (1ULL << 7),
    IO_EXPANDER_PIN_NUM_8  = (1ULL << 8),
} esp_io_expander_pin_num_t;

typedef void *esp_io_expander_handle_t;

extern esp_err_t mock_esp_io_expander_print_state_return;
extern int mock_esp_io_expander_print_state_called;
extern esp_io_expander_handle_t mock_esp_io_expander_print_state_handle;

extern int mock_esp_io_expander_set_dir_called;
extern esp_io_expander_handle_t mock_esp_io_expander_set_dir_handle;
extern uint32_t mock_esp_io_expander_set_dir_pin_masks[MAX_FUNCTION_CALLS];
extern esp_io_expander_dir_t mock_esp_io_expander_set_dir_directions[MAX_FUNCTION_CALLS];

extern int mock_esp_io_expander_set_level_called;
extern esp_io_expander_handle_t mock_esp_io_expander_set_level_handle;
extern uint32_t mock_esp_io_expander_set_level_pin_masks[MAX_FUNCTION_CALLS];
extern uint8_t mock_esp_io_expander_set_level_levels[MAX_FUNCTION_CALLS];

esp_err_t esp_io_expander_print_state(esp_io_expander_handle_t handle);
esp_err_t esp_io_expander_set_dir(esp_io_expander_handle_t handle, uint32_t pin_num_mask, esp_io_expander_dir_t direction);
esp_err_t esp_io_expander_set_level(esp_io_expander_handle_t handle, uint32_t pin_num_mask, uint8_t level);
