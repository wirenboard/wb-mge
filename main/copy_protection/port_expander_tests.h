#pragma once

#include "esp_err.h"

// Сan only be called after gpio_expander_init()
esp_err_t port_expander_run_tests(void);
