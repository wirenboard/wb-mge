#pragma once
#include "esp_err.h"
#define UART_PIN_NO_CHANGE (-1)
typedef int uart_port_t;
esp_err_t uart_set_pin(uart_port_t uart_num, int tx_io_num, int rx_io_num, int rts_io_num, int cts_io_num);
