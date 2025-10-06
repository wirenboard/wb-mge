#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_io_expander_tca95xx_16bit.h"

typedef enum {
    RS485_1 = 1,
    RS485_2 = 2
} rs485_port_t;

void rs485_control_init(esp_io_expander_handle_t io_expander_handle);

void rs485_term_on_off(rs485_port_t port, bool on);
void rs485_pupd_on_off(rs485_port_t port, bool on);
void rs485_bus_vout_on_off(bool on);
void rs485_bus_vout_set_allowed(bool allowed);
