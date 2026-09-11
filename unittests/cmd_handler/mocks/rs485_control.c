/* rs485_control mock for the cmd_handler unit test. The reboot task drops the RS-485 V-out before
 * restarting the chip; the tests never let that task run, so the mock only has to satisfy the
 * linker and record what it was asked for. */

#include "rs485_control.h"

int mock_rs485_bus_vout_on_off_called = 0;
int mock_rs485_bus_vout_set_allowed_called = 0;

void mock_rs485_control_reset(void)
{
    mock_rs485_bus_vout_on_off_called = 0;
    mock_rs485_bus_vout_set_allowed_called = 0;
}

esp_err_t rs485_bus_vout_on_off(bool on)
{
    (void)on;
    mock_rs485_bus_vout_on_off_called++;
    return ESP_OK;
}

esp_err_t rs485_bus_vout_set_allowed(bool allowed)
{
    (void)allowed;
    mock_rs485_bus_vout_set_allowed_called++;
    return ESP_OK;
}
