#include "mb_slave.h"

#include <string.h>

int      mock_mb_slave_set_tx_disabled_called = 0;
unsigned mock_mb_slave_set_tx_disabled_indexes[MOCK_MB_SLAVE_MAX_CALLS];
bool     mock_mb_slave_set_tx_disabled_values[MOCK_MB_SLAVE_MAX_CALLS];

esp_err_t mb_slave_start(void)
{
    return ESP_OK;
}

esp_err_t mb_slave_stop(void)
{
    return ESP_OK;
}

bool mb_slave_check_settings_changed(void)
{
    return false;
}

esp_err_t mb_slave_set_tx_disabled(unsigned index, bool disabled)
{
    if (mock_mb_slave_set_tx_disabled_called < MOCK_MB_SLAVE_MAX_CALLS) {
        mock_mb_slave_set_tx_disabled_indexes[mock_mb_slave_set_tx_disabled_called] = index;
        mock_mb_slave_set_tx_disabled_values[mock_mb_slave_set_tx_disabled_called] = disabled;
    }
    mock_mb_slave_set_tx_disabled_called++;
    return ESP_OK;
}

void mock_mb_slave_reset(void)
{
    mock_mb_slave_set_tx_disabled_called = 0;
    memset(mock_mb_slave_set_tx_disabled_indexes, 0, sizeof(mock_mb_slave_set_tx_disabled_indexes));
    memset(mock_mb_slave_set_tx_disabled_values, 0, sizeof(mock_mb_slave_set_tx_disabled_values));
}
