#pragma once

#include <stdbool.h>
#include "esp_err.h"

void update_rs485_control(void);
esp_err_t update_rs485_vout(bool enabled);
void update_io_bus_control(void);
void update_serial_tx_disabled(void);
