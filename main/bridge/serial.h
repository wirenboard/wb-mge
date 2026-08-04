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

// When adding/removing a field, update bridge_config_equal() in bridge.c — it
// compares serial_config_t field by field (memcmp is unsafe due to struct padding).
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
    // TX/RX pins are kept here (not just in serial_config_t) because serial_set_tx_disabled()
    // has to re-apply the full pin routing when it hands the port back to the UART.
    // See the comment in serial_set_tx_disabled() in serial.c for why.
    int tx_pin;
    int rx_pin;
    int dir_pin;            // Direction pin used for RS-485 half-duplex control
    // When true, serial_send() returns without transmitting. Plain bool, not _Atomic, but every access uses a
    // GCC atomic builtin on BOTH sides — serial_tx_disabled() below to read, __atomic_store_n(RELEASE) in
    // serial_set_tx_disabled() to write, as for sniff_handler here — because a plain store racing an
    // __atomic_load_n() is a C11 data race however it compiles on Xtensa, and no single lock covers both sides:
    // the writers, port_manager_set_tx_disabled() (httpd task, plus the button task on factory reset) and
    // port_init_mode(), always hold that port's pm_lock — C10 put port_manager_init()'s boot loop
    // under it too, so no unlocked writer is left — while the readers hold nothing (serial_send() from the
    // repeater and modbus_tcp_server_task), a disjoint lock (repeater_rx_handler()'s s_lock pre-check,
    // transparent-TCP's send under serial_path_lock), or, in port_manager_send_raw() alone, pm_lock. mge_v3 is
    // dual-core (CONFIG_FREERTOS_UNICORE unset, sdkconfig.mge_v3:1231), so this is a real cross-core race, not
    // just preemption; the single-core QEMU build cannot reproduce it. RELEASE/ACQUIRE because each store lands
    // AFTER the pin work it describes (dir_pin parked LOW, or the routing restored) and so publishes consistent
    // hardware state to whichever reader picks it up; serialising the writers against each other is pm_lock's
    // job, not this pairing's. Not SEQ_CST: nothing else is correlated with this flag. The one plain access
    // left is serial_init()'s pre-publication store.
    bool tx_disabled;
    bool wait_for_idle;     // When true, receive_handler is called only on idle timeout (Modbus RTU frame boundary)
    QueueHandle_t uart_queue;
    serial_receive_handler_t receive_handler;
    serial_receive_handler_t sniff_handler;
    serial_drop_handler_t drop_handler;  // Optional: called with the count of bytes dropped on RX overflow (NULL = not counted)
    TaskHandle_t task_handle;
    EventGroupHandle_t event_group;
};

/* Read the TX-disabled flag — the only sanctioned reader of desc->tx_disabled. ACQUIRE pairs with
 * the RELEASE store in serial_set_tx_disabled(), per writer; the note on the field above says who
 * races whom and what that pairing does and does not buy. */
static inline bool serial_tx_disabled(const serial_desc_t *desc)
{
    return __atomic_load_n(&desc->tx_disabled, __ATOMIC_ACQUIRE);
}

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
