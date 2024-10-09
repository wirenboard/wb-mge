#pragma once

#include <stdint.h>

#include "driver/uart.h"
#include "esp_err.h"

typedef struct {
    uint32_t baudrate;
    uart_parity_t parity;
    uart_stop_bits_t stopbits;
    uart_word_length_t databits;
} serial_config_t;

typedef void (*serial_receive_handler_t)(uint8_t *, size_t);

esp_err_t serial_init(serial_config_t *serial_config, serial_receive_handler_t serial_receive_handler);
esp_err_t serial_send(uint8_t *data, size_t len);
