#include "sniffer.h"
#include "bridge.h"  /* for BRIDGES_COUNT */
#include <string.h>

/* Call tracking variables exposed for test assertions */
int mock_sniffer_init_called = 0;
int mock_sniffer_attach_called[BRIDGES_COUNT];
int mock_sniffer_detach_called[BRIDGES_COUNT];
int mock_sniffer_enable_called[BRIDGES_COUNT];
int mock_sniffer_disable_called[BRIDGES_COUNT];
/* Per-port reason bitmask reflecting enable/disable calls (mirrors production) */
uint8_t mock_sniffer_reasons[BRIDGES_COUNT];
/* Last reason argument passed to enable/disable, per port */
uint8_t mock_sniffer_enable_last_reason[BRIDGES_COUNT];
uint8_t mock_sniffer_disable_last_reason[BRIDGES_COUNT];

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
        mock_sniffer_reasons[port_index] = 0;
    }
}

void sniffer_enable(unsigned port_index, sniff_reason_t reason)
{
    if (port_index < BRIDGES_COUNT) {
        mock_sniffer_enable_called[port_index]++;
        mock_sniffer_enable_last_reason[port_index] = (uint8_t)reason;
        mock_sniffer_reasons[port_index] |= (uint8_t)reason;
    }
}

void sniffer_disable(unsigned port_index, sniff_reason_t reason)
{
    if (port_index < BRIDGES_COUNT) {
        mock_sniffer_disable_called[port_index]++;
        mock_sniffer_disable_last_reason[port_index] = (uint8_t)reason;
        mock_sniffer_reasons[port_index] &= (uint8_t)~(uint8_t)reason;
    }
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
    memset(mock_sniffer_disable_called, 0, sizeof(mock_sniffer_disable_called));
    memset(mock_sniffer_reasons, 0, sizeof(mock_sniffer_reasons));
    memset(mock_sniffer_enable_last_reason, 0, sizeof(mock_sniffer_enable_last_reason));
    memset(mock_sniffer_disable_last_reason, 0, sizeof(mock_sniffer_disable_last_reason));
}
