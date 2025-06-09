#pragma once

#include <stdint.h>

#include "driver/uart.h"
#include "esp_err.h"

typedef struct {
    uart_port_t port_num;
    int tx_pin;
    int rx_pin;
    int dir_pin;

    uint32_t baudrate;
    uart_parity_t parity;
    uart_stop_bits_t stopbits;
    uart_word_length_t databits;
} serial_config_t;

typedef struct serial_desc_t serial_desc_t;

typedef void (*serial_receive_handler_t)(serial_desc_t *desc, uint8_t *, size_t);

struct serial_desc_t {
    uart_port_t port_num;
    QueueHandle_t uart_queue;
    serial_receive_handler_t receive_handler;
};

serial_desc_t* serial_init(serial_config_t *serial_config, serial_receive_handler_t serial_receive_handler);
esp_err_t serial_send(serial_desc_t *desc, uint8_t *data, size_t len);
