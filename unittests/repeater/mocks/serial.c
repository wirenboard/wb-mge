// Mock implementation of serial for the repeater unit tests.
// Hands back a distinct descriptor per serial_init() call so the repeater RX
// handler can map a descriptor back to its port index. Captures the registered
// receive_handler and records serial_send() calls for forwarding assertions.

#include "serial.h"
#include "mock_serial.h"
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
    return ESP_OK;
}

esp_err_t serial_set_rx_timeout(serial_desc_t *desc, uint8_t tout_symbols)
{
    (void)desc;
    (void)tout_symbols;
    return ESP_OK;
}
