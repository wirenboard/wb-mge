#pragma once

#include "esp_err.h"

typedef void *esp_io_expander_handle_t;

esp_err_t esp_io_expander_print_state(esp_io_expander_handle_t handle);
