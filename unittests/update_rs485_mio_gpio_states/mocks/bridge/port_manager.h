#pragma once

#include "esp_err.h"
#include <stdbool.h>

// Minimal mock header for port_manager, used by update_rs485_mio_gpio_states tests.
// Only declares the function(s) called by update_rs485_control().
esp_err_t port_manager_set_tx_disabled(unsigned port_index, bool disabled);
