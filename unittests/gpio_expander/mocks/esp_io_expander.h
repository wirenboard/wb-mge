#pragma once

#include "esp_err.h"

typedef void *esp_io_expander_handle_t;

extern esp_err_t mock_esp_io_expander_print_state_return;
extern int mock_esp_io_expander_print_state_called;
extern esp_io_expander_handle_t mock_esp_io_expander_print_state_handle;

esp_err_t esp_io_expander_print_state(esp_io_expander_handle_t handle);
