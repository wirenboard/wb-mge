// Mock implementation of serial for the repeater unit tests.
// Hands back a distinct descriptor per serial_init() call so the repeater RX
// handler can map a descriptor back to its port index. Captures the registered
// receive_handler and records serial_send() calls for forwarding assertions.

#include "serial.h"
#include "mock_serial.h"
#include "call_sequence.h"
#include <string.h>

// One descriptor per port; index = order of serial_init() calls.
static serial_desc_t mock_serial_descs[BRIDGES_COUNT];
static unsigned mock_serial_next_desc;

mock_serial_calls_t mock_serial_calls = {0};
serial_receive_handler_t mock_serial_registered_handler = 0;

serial_desc_t *mock_serial_get_desc(unsigned init_order)
{
    if (init_order >= BRIDGES_COUNT) {
        return 0;
    }
    return &mock_serial_descs[init_order];
}

void mock_serial_reset(void)
{
    memset(&mock_serial_calls, 0, sizeof(mock_serial_calls));
    mock_serial_calls.send_ret = ESP_OK;
    memset(mock_serial_descs, 0, sizeof(mock_serial_descs));
    mock_serial_next_desc = 0;
    mock_serial_registered_handler = 0;
}

serial_desc_t *serial_init(serial_config_t *serial_config, serial_receive_handler_t serial_receive_handler)
{
    (void)serial_config;
    mock_serial_calls.init_called++;
    if (mock_serial_calls.init_should_fail) {
        return NULL;
    }
    mock_serial_registered_handler = serial_receive_handler;
    if (mock_serial_next_desc >= BRIDGES_COUNT) {
        // Out of pre-allocated descriptors; return the last one to avoid OOB.
        return &mock_serial_descs[BRIDGES_COUNT - 1];
    }
    return &mock_serial_descs[mock_serial_next_desc++];
}

esp_err_t serial_send(serial_desc_t *desc, uint8_t *data, size_t len)
{
    mock_serial_calls.send_called++;
    mock_serial_calls.send_last_desc = desc;
    mock_serial_calls.send_last_data = data;
    mock_serial_calls.send_last_len = len;
    // Stands in for the time the real serial_send() spends inside uart_write_bytes(): the
    // hook runs while the caller is "on the wire", so a test can inspect the repeater state
    // that only exists in that window. See mock_serial.h.
    if (mock_serial_calls.send_hook != 0) {
        mock_serial_calls.send_hook(desc, data, len);
    }
    return mock_serial_calls.send_ret;
}

esp_err_t serial_wait_tx_done(serial_desc_t *desc, TickType_t timeout_ticks)
{
    (void)desc;
    (void)timeout_ticks;
    return ESP_OK;
}

esp_err_t serial_deinit(serial_desc_t *desc)
{
    (void)desc;
    mock_serial_calls.deinit_called++;
    mock_serial_calls.deinit_call_seq = call_sequence_get_call_id();
    return ESP_OK;
}

esp_err_t serial_set_rx_timeout(serial_desc_t *desc, uint8_t tout_symbols)
{
    (void)desc;
    (void)tout_symbols;
    return ESP_OK;
}

// repeater.c never calls this; the tests do, so they flip a descriptor's TX gate through the same
// release store production uses instead of poking the field (see the note on tx_disabled in serial.h).
// The NULL guard mirrors the real one; its pin work has no counterpart in a mock with no UART behind it.
esp_err_t serial_set_tx_disabled(serial_desc_t *desc, bool disabled)
{
    if (desc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    __atomic_store_n(&desc->tx_disabled, disabled, __ATOMIC_RELEASE);
    return ESP_OK;
}
