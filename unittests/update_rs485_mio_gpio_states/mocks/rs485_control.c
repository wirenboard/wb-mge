#include "rs485_control.h"

#include <string.h>
#include <stddef.h>

int mock_rs485_pupd_on_off_called = 0;
rs485_port_t mock_rs485_pupd_on_off_ports[MAX_FUNCTION_CALLS];
bool mock_rs485_pupd_on_off_on_values[MAX_FUNCTION_CALLS];

int mock_rs485_term_on_off_called = 0;
rs485_port_t mock_rs485_term_on_off_ports[MAX_FUNCTION_CALLS];
bool mock_rs485_term_on_off_on_values[MAX_FUNCTION_CALLS];

int mock_rs485_bus_vout_on_off_called = 0;
bool mock_rs485_bus_vout_on_off_on_values[MAX_FUNCTION_CALLS];

esp_err_t rs485_pupd_on_off(rs485_port_t port, bool on)
{
    if (mock_rs485_pupd_on_off_called < MAX_FUNCTION_CALLS) {
        mock_rs485_pupd_on_off_ports[mock_rs485_pupd_on_off_called] = port;
        mock_rs485_pupd_on_off_on_values[mock_rs485_pupd_on_off_called] = on;
    }
    mock_rs485_pupd_on_off_called++;
    return ESP_OK;
}

esp_err_t rs485_term_on_off(rs485_port_t port, bool on)
{
    if (mock_rs485_term_on_off_called < MAX_FUNCTION_CALLS) {
        mock_rs485_term_on_off_ports[mock_rs485_term_on_off_called] = port;
        mock_rs485_term_on_off_on_values[mock_rs485_term_on_off_called] = on;
    }
    mock_rs485_term_on_off_called++;
    return ESP_OK;
}

esp_err_t rs485_bus_vout_on_off(bool on)
{
    if (mock_rs485_bus_vout_on_off_called < MAX_FUNCTION_CALLS) {
        mock_rs485_bus_vout_on_off_on_values[mock_rs485_bus_vout_on_off_called] = on;
    }
    mock_rs485_bus_vout_on_off_called++;
    return ESP_OK;
}

void mock_rs485_control_reset(void)
{
    mock_rs485_pupd_on_off_called = 0;
    memset(mock_rs485_pupd_on_off_ports, 0, sizeof(mock_rs485_pupd_on_off_ports));
    memset(mock_rs485_pupd_on_off_on_values, 0, sizeof(mock_rs485_pupd_on_off_on_values));

    mock_rs485_term_on_off_called = 0;
    memset(mock_rs485_term_on_off_ports, 0, sizeof(mock_rs485_term_on_off_ports));
    memset(mock_rs485_term_on_off_on_values, 0, sizeof(mock_rs485_term_on_off_on_values));

    mock_rs485_bus_vout_on_off_called = 0;
    memset(mock_rs485_bus_vout_on_off_on_values, 0, sizeof(mock_rs485_bus_vout_on_off_on_values));
}
