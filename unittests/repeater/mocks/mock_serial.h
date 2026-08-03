#pragma once

#include "serial.h"
#include "bridge.h"   // BRIDGES_COUNT
#include <stdbool.h>

// Mock serial for the repeater unit tests. Each serial_init() call hands back a
// distinct descriptor (one per port) so the repeater RX handler can distinguish
// ports via its find-by-serial-desc lookup. The receive_handler passed to
// serial_init() is captured so tests can drive the data path directly.
typedef struct {
    int init_called;
    int deinit_called;
    unsigned deinit_call_seq;   // call_sequence id captured in serial_deinit() (R2: lock-order assertion)
    bool init_should_fail;
    // serial_send observation (per most-recent call).
    int send_called;
    serial_desc_t *send_last_desc;
    uint8_t *send_last_data;
    size_t send_last_len;
    // Return value the next serial_send() call(s) should yield. Default ESP_OK.
    esp_err_t send_ret;
    // Called from INSIDE serial_send(), before it returns — i.e. in the window where the real
    // implementation would be blocked in uart_write_bytes(). It is the only place a test can
    // observe state that exists only while a forward is on the wire: whether the repeater lock
    // is held (mock_xSemaphore_held_count) and whether the destination port is registered as
    // in-flight. NULL (the default, restored by mock_serial_reset()) means no hook.
    void (*send_hook)(serial_desc_t *desc, uint8_t *data, size_t len);
} mock_serial_calls_t;

extern mock_serial_calls_t mock_serial_calls;

// Handler captured from serial_init (the repeater repeater_rx_handler callback).
extern serial_receive_handler_t mock_serial_registered_handler;

// Return the descriptor that serial_init() handed back for the given init order
// (0 = first serial_init() call, 1 = second). Stable across the test.
serial_desc_t *mock_serial_get_desc(unsigned init_order);

void mock_serial_reset(void);
