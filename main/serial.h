#pragma once

#include <stdint.h>

#include "driver/uart.h"
#include "esp_err.h"

#define SERIAL_PORT_NUM_1             2
#define SERIAL_INPUT_PIN_1            GPIO_NUM_12
#define SERIAL_OUTPUT_PIN_1           GPIO_NUM_14
#define SERIAL_IO_PIN_1               GPIO_NUM_15

#define SERIAL_PORT_NUM_2             1
#define SERIAL_INPUT_PIN_2            GPIO_NUM_9
#define SERIAL_OUTPUT_PIN_2           GPIO_NUM_10
#define SERIAL_IO_PIN_2               GPIO_NUM_4

typedef struct {
    int tx_pin;
    int rx_pin;
    int dir_pin;

    uint32_t baudrate;
    uart_parity_t parity;
    uart_stop_bits_t stopbits;
    uart_word_length_t databits;
} serial_config_t;

typedef struct {
    uart_port_t port_num;
    QueueHandle_t uart_queue;
    serial_receive_handler_t receive_handler;
} serial_port_desc_t;

typedef void (*serial_receive_handler_t)(uart_port_t port_num, uint8_t *, size_t);

serial_port_desc_t* serial_init(uart_port_t port_num, serial_config_t *serial_config, serial_receive_handler_t serial_receive_handler);
esp_err_t serial_send(serial_port_desc_t *desc, uint8_t *data, size_t len);
