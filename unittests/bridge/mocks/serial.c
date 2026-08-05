// Stub implementation of serial.c for bridge unit tests.
// Provides minimal no-op versions of all serial API functions so that
// bridge.c can link without the real UART driver.

#include "serial.h"
#include <stdbool.h>
#include <string.h>

static serial_desc_t mock_serial_desc_instance; // Static storage for mock descriptor
bool mock_serial_init_should_succeed = false;   // false by default: returns NULL

serial_desc_t *serial_init(serial_config_t *serial_config, serial_receive_handler_t serial_receive_handler)
{
    (void)serial_config;
    (void)serial_receive_handler;
    if (mock_serial_init_should_succeed) {
        return &mock_serial_desc_instance;
    }
    return NULL;
}

void mock_serial_reset(void)
{
    mock_serial_init_should_succeed = false;
    memset(&mock_serial_desc_instance, 0, sizeof(mock_serial_desc_instance));
}

esp_err_t serial_send(serial_desc_t *desc, uint8_t *data, size_t len)
{
    (void)desc;
    (void)data;
    (void)len;
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
    return ESP_OK;
}

esp_err_t serial_set_rx_timeout(serial_desc_t *desc, uint8_t tout_symbols)
{
    (void)desc;
    (void)tout_symbols;
    return ESP_OK;
}
