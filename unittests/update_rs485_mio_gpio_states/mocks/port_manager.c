#include "bridge/port_manager.h"
#include <string.h>

int mock_port_manager_set_tx_disabled_called = 0;
unsigned mock_port_manager_set_tx_disabled_port[8];
bool mock_port_manager_set_tx_disabled_value[8];

esp_err_t port_manager_set_tx_disabled(unsigned port_index, bool disabled)
{
    int idx = mock_port_manager_set_tx_disabled_called;
    mock_port_manager_set_tx_disabled_called++;
    if (idx < 8) {
        mock_port_manager_set_tx_disabled_port[idx] = port_index;
        mock_port_manager_set_tx_disabled_value[idx] = disabled;
    }
    return ESP_OK;
}

void mock_port_manager_reset(void)
{
    mock_port_manager_set_tx_disabled_called = 0;
    memset(mock_port_manager_set_tx_disabled_port, 0, sizeof(mock_port_manager_set_tx_disabled_port));
    memset(mock_port_manager_set_tx_disabled_value, 0, sizeof(mock_port_manager_set_tx_disabled_value));
}
