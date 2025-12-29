#pragma once

#include "esp_err.h"
#include <stdbool.h>

#define MAX_FUNCTION_CALLS                  20
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

typedef struct {
    int called;
    unsigned call_seq;
    esp_io_expander_handle_t handle;
    bool should_fail;
} mock_esp_io_expander_print_state_t;

typedef struct {
    int called;
    unsigned call_seq;
    esp_io_expander_handle_t handle;
    uint32_t masks[MAX_FUNCTION_CALLS];
    esp_io_expander_dir_t directions[MAX_FUNCTION_CALLS];
    bool should_fail;
} mock_esp_io_expander_set_dir_t;

typedef struct {
    int called;
    unsigned call_seq;
    esp_io_expander_handle_t handle;
    uint32_t masks[MAX_FUNCTION_CALLS];
    uint8_t levels[MAX_FUNCTION_CALLS];
    bool should_fail;
} mock_esp_io_expander_set_level_t;

typedef struct {
    int called;
    unsigned call_seq;
    esp_io_expander_handle_t handle;
    uint32_t masks[MAX_FUNCTION_CALLS];
    uint32_t levels_setup;
    bool should_fail;
} mock_esp_io_expander_get_level_t;

typedef struct {
    int called;
    unsigned call_seq;
    esp_io_expander_handle_t handle;
    bool should_fail;
} mock_esp_io_expander_del_t;

extern mock_esp_io_expander_print_state_t mock_esp_io_expander_print_state_data;
extern mock_esp_io_expander_set_dir_t mock_esp_io_expander_set_dir_data;
extern mock_esp_io_expander_set_level_t mock_esp_io_expander_set_level_data;
extern mock_esp_io_expander_get_level_t mock_esp_io_expander_get_level_data;
extern mock_esp_io_expander_del_t mock_esp_io_expander_del_data;

esp_err_t esp_io_expander_print_state(esp_io_expander_handle_t handle);
esp_err_t esp_io_expander_set_dir(esp_io_expander_handle_t handle, uint32_t pin_num_mask, esp_io_expander_dir_t direction);
esp_err_t esp_io_expander_set_level(esp_io_expander_handle_t handle, uint32_t pin_num_mask, uint8_t level);
esp_err_t esp_io_expander_get_level(esp_io_expander_handle_t handle, uint32_t pin_num_mask, uint32_t *level_mask);
esp_err_t esp_io_expander_del(esp_io_expander_handle_t handle);

void mock_esp_io_expander_reset(void);
