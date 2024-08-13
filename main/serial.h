#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/uart.h"

typedef void(*serial_receive_handler_t)(uint8_t*, uint8_t);

esp_err_t serial_init(uart_config_t *uart_config, serial_receive_handler_t serial_receive_handler);
esp_err_t serial_send(uint8_t *data, uint8_t len);
