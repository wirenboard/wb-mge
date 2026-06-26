#pragma once

#include <stdint.h>
#include <stdbool.h>

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

// Invoked when received bytes are dropped at the RX stage (buffer/ring overflow). dropped_len = bytes discarded.
typedef void (*serial_drop_handler_t)(serial_desc_t *desc, size_t dropped_len);

struct serial_desc_t {
    uart_port_t port_num;
    int dir_pin;            // Direction pin used for RS-485 half-duplex control
    bool tx_disabled;       // When true, serial_send() returns immediately without transmitting
    bool wait_for_idle;     // When true, receive_handler is called only on idle timeout (Modbus RTU frame boundary)
    QueueHandle_t uart_queue;
    serial_receive_handler_t receive_handler;
    serial_receive_handler_t sniff_handler;
    serial_drop_handler_t drop_handler;  // Optional: called with the count of bytes dropped on RX overflow (NULL = not counted)
    TaskHandle_t task_handle;
    EventGroupHandle_t event_group;
};

serial_desc_t* serial_init(serial_config_t *serial_config, serial_receive_handler_t serial_receive_handler);
esp_err_t serial_send(serial_desc_t *desc, uint8_t *data, size_t len);
esp_err_t serial_wait_tx_done(serial_desc_t *desc, TickType_t timeout_ticks);
esp_err_t serial_deinit(serial_desc_t *desc);
esp_err_t serial_set_rx_timeout(serial_desc_t *desc, uint8_t tout_symbols);
esp_err_t serial_set_tx_disabled(serial_desc_t *desc, bool disabled);

#ifdef __unittest_env__
/* Exposed for unit tests: run uart_event_task inline on the given descriptor.
 * Allows tests to set desc->wait_for_idle before processing queued UART events. */
void serial_test_run_uart_event_task(serial_desc_t *desc);
#endif /* __unittest_env__ */
