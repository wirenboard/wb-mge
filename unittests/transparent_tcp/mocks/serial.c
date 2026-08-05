// Mock implementation of serial for transparent_tcp unit tests.
// Tracks call counts and can be configured to fail (return NULL) from serial_init.

#include "serial.h"
#include "mock_serial.h"
#include "call_sequence.h"
#include <string.h>

static serial_desc_t mock_serial_desc;
mock_serial_calls_t mock_serial_calls = {0};
serial_receive_handler_t mock_serial_registered_handler = 0;

serial_desc_t *mock_serial_get_desc(void)
{
    return &mock_serial_desc;
}

void mock_serial_reset(void)
{
    memset(&mock_serial_calls, 0, sizeof(mock_serial_calls));
    memset(&mock_serial_desc, 0, sizeof(mock_serial_desc));
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
    return &mock_serial_desc;
}

esp_err_t serial_send(serial_desc_t *desc, uint8_t *data, size_t len)
{
    (void)desc;
    mock_serial_calls.send_called++;
    mock_serial_calls.send_last_data = data;
    mock_serial_calls.send_last_len = len;
    return ESP_OK;
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
