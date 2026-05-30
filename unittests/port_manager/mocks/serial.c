#include "serial.h"
#include "bridge.h"       /* for BRIDGES_COUNT */
#include "bridge_mock.h"  /* for mock_serial_desc_instances[] */
#include <string.h>

/* Call tracking variables exposed for test assertions */
int mock_serial_deinit_called[BRIDGES_COUNT];
int mock_serial_set_rx_timeout_called[BRIDGES_COUNT];
/* Last RX timeout value passed to serial_set_rx_timeout(), keyed by port index */
uint8_t mock_serial_set_rx_timeout_value[BRIDGES_COUNT];
serial_desc_t *mock_serial_deinit_desc[BRIDGES_COUNT];

/* serial_send tracking */
int mock_serial_send_called;
uint8_t mock_serial_send_last_data[256]; /* MODBUS_RTU_MAX_FRAME_LEN = 256 */
size_t mock_serial_send_last_len;
/* Return value the next serial_send() call(s) should yield. Default ESP_OK. */
esp_err_t mock_serial_send_ret = ESP_OK;

serial_desc_t *serial_init(serial_config_t *serial_config,
                            serial_receive_handler_t serial_receive_handler)
{
    (void)serial_config;
    (void)serial_receive_handler;
    return NULL;
}

esp_err_t serial_send(serial_desc_t *desc, uint8_t *data, size_t len)
{
    (void)desc;
    mock_serial_send_called++;
    if (data && len > 0 && len <= 256) {
        memcpy(mock_serial_send_last_data, data, len);
    }
    mock_serial_send_last_len = len;
    return mock_serial_send_ret;
}

esp_err_t serial_wait_tx_done(serial_desc_t *desc, TickType_t timeout_ticks)
{
    (void)desc;
    (void)timeout_ticks;
    return ESP_OK;
}

esp_err_t serial_deinit(serial_desc_t *desc)
{
    /* Map the descriptor pointer back to its port index by comparing against
     * the known mock_serial_desc_instances[] allocated by bridge mock. */
    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        if (desc == &mock_serial_desc_instances[i]) {
            mock_serial_deinit_called[i]++;
            mock_serial_deinit_desc[i] = desc;
            return ESP_OK;
        }
    }
    /* Descriptor did not match any known instance — increment slot 0 as fallback */
    mock_serial_deinit_called[0]++;
    mock_serial_deinit_desc[0] = desc;
    return ESP_OK;
}

esp_err_t serial_set_rx_timeout(serial_desc_t *desc, uint8_t tout_symbols)
{
    /* Map the descriptor pointer back to its port index (same scheme as
     * serial_deinit) so tests can assert per-port count and value. Both PASSIVE
     * and TCP_BRIDGE use &mock_serial_desc_instances[index] as the descriptor. */
    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        if (desc == &mock_serial_desc_instances[i]) {
            mock_serial_set_rx_timeout_called[i]++;
            mock_serial_set_rx_timeout_value[i] = tout_symbols;
            return ESP_OK;
        }
    }
    /* Descriptor did not match any known instance — fall back to slot 0 */
    mock_serial_set_rx_timeout_called[0]++;
    mock_serial_set_rx_timeout_value[0] = tout_symbols;
    return ESP_OK;
}

esp_err_t serial_set_tx_disabled(serial_desc_t *desc, bool disabled)
{
    if (desc) {
        desc->tx_disabled = disabled;
    }
    return ESP_OK;
}

void mock_serial_reset(void)
{
    memset(mock_serial_deinit_called, 0, sizeof(mock_serial_deinit_called));
    memset(mock_serial_set_rx_timeout_called, 0, sizeof(mock_serial_set_rx_timeout_called));
    memset(mock_serial_set_rx_timeout_value, 0, sizeof(mock_serial_set_rx_timeout_value));
    memset(mock_serial_deinit_desc, 0, sizeof(mock_serial_deinit_desc));
    mock_serial_send_called = 0;
    memset(mock_serial_send_last_data, 0, sizeof(mock_serial_send_last_data));
    mock_serial_send_last_len = 0;
    mock_serial_send_ret = ESP_OK;
}
