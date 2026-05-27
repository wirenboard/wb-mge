#include "sniffer.h"
#include "bridge.h"  /* for BRIDGES_COUNT */
#include <string.h>

/* Call tracking variables exposed for test assertions */
int mock_sniffer_init_called = 0;
int mock_sniffer_attach_called[BRIDGES_COUNT];
int mock_sniffer_detach_called[BRIDGES_COUNT];
int mock_sniffer_enable_called[BRIDGES_COUNT];
bool mock_sniffer_set_cache_active_value = false;
int mock_sniffer_set_cache_active_called = 0;

/* sniffer_inject_tx tracking */
int mock_sniffer_inject_tx_called[BRIDGES_COUNT];
uint8_t mock_sniffer_inject_tx_last_data[BRIDGES_COUNT][256];
size_t mock_sniffer_inject_tx_last_len[BRIDGES_COUNT];

esp_err_t sniffer_init(void)
{
    mock_sniffer_init_called++;
    return ESP_OK;
}

void sniffer_attach(unsigned port_index, serial_desc_t *sd)
{
    (void)sd;
    if (port_index < BRIDGES_COUNT) {
        mock_sniffer_attach_called[port_index]++;
    }
}

void sniffer_detach(unsigned port_index)
{
    if (port_index < BRIDGES_COUNT) {
        mock_sniffer_detach_called[port_index]++;
    }
}

void sniffer_enable(unsigned port_index)
{
    if (port_index < BRIDGES_COUNT) {
        mock_sniffer_enable_called[port_index]++;
    }
}

void sniffer_disable(unsigned port_index)
{
    (void)port_index;
}

void sniffer_set_cache_active(bool active)
{
    mock_sniffer_set_cache_active_called++;
    mock_sniffer_set_cache_active_value = active;
}

void sniffer_inject_tx(unsigned port_index, const uint8_t *data, size_t len)
{
    if (port_index >= BRIDGES_COUNT) return;
    mock_sniffer_inject_tx_called[port_index]++;
    if (data && len > 0 && len <= 256) {
        memcpy(mock_sniffer_inject_tx_last_data[port_index], data, len);
    }
    mock_sniffer_inject_tx_last_len[port_index] = len;
}

esp_err_t sniffer_register_handlers(httpd_handle_t s)
{
    (void)s;
    return ESP_OK;
}

void mock_sniffer_reset(void)
{
    mock_sniffer_init_called = 0;
    memset(mock_sniffer_attach_called, 0, sizeof(mock_sniffer_attach_called));
    memset(mock_sniffer_detach_called, 0, sizeof(mock_sniffer_detach_called));
    memset(mock_sniffer_enable_called, 0, sizeof(mock_sniffer_enable_called));
    mock_sniffer_set_cache_active_value = false;
    mock_sniffer_set_cache_active_called = 0;
    memset(mock_sniffer_inject_tx_called, 0, sizeof(mock_sniffer_inject_tx_called));
    memset(mock_sniffer_inject_tx_last_data, 0, sizeof(mock_sniffer_inject_tx_last_data));
    memset(mock_sniffer_inject_tx_last_len, 0, sizeof(mock_sniffer_inject_tx_last_len));
}
