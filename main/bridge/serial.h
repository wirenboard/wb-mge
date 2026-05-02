#pragma once

#include <stdint.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// RX timeout in UART symbol periods for sniffer mode (Modbus packet boundary detection)
#define SERIAL_RX_TOUT_SNIFFER  3
// RX timeout in UART symbol periods for transparent proxy mode (minimal latency)
#define SERIAL_RX_TOUT_PROXY    10

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
    serial_receive_handler_t sniff_handler;
    TaskHandle_t task_handle;
    EventGroupHandle_t event_group;
};

serial_desc_t* serial_init(serial_config_t *serial_config, serial_receive_handler_t serial_receive_handler);
esp_err_t serial_send(serial_desc_t *desc, uint8_t *data, size_t len);
esp_err_t serial_wait_tx_done(serial_desc_t *desc, TickType_t timeout_ticks);
esp_err_t serial_deinit(serial_desc_t *desc);
esp_err_t serial_set_rx_timeout(serial_desc_t *desc, uint8_t tout_symbols);
