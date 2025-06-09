#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_io_expander_tca95xx_16bit.h"

void rs485_control_init(esp_io_expander_handle_t io_expander_handle);

void rs485_term_on_off(uint8_t port, bool on);
void rs485_pupd_on_off(uint8_t port, bool on);
void rs485_bus_vout_on_off(bool on);
