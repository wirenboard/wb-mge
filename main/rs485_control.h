#pragma once

#include "esp_io_expander.h"
#include <stdbool.h>

typedef enum {
    RS485_1 = 1,
    RS485_2 = 2
} rs485_port_t;

esp_err_t rs485_control_init(esp_io_expander_handle_t io_expander_handle);

esp_err_t rs485_term_on_off(rs485_port_t port, bool on);
esp_err_t rs485_pupd_on_off(rs485_port_t port, bool on);
esp_err_t rs485_bus_vout_on_off(bool on);
esp_err_t rs485_bus_vout_set_allowed(bool allowed);
