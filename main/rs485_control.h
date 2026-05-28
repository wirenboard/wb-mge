#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    RS485_1 = 1,
    RS485_2 = 2
} rs485_port_t;

// Can only be called after gpio_expander_init()
esp_err_t rs485_control_init(void);

esp_err_t rs485_term_on_off(rs485_port_t port, bool on);
esp_err_t rs485_pupd_on_off(rs485_port_t port, bool on);
esp_err_t rs485_bus_vout_on_off(bool on);
esp_err_t rs485_bus_vout_set_allowed(bool allowed);
