#include "modbus_tcp.h"
#include <string.h>

int mock_modbus_tcp_init_port_called = 0;
esp_err_t mock_modbus_tcp_init_port_return_value = ESP_OK;
unsigned mock_modbus_tcp_init_port_indices[MOCK_MODBUS_TCP_MAX_CALLS] = {0};
serial_config_t *mock_modbus_tcp_config = NULL;
bridge_mode_t mock_modbus_tcp_mode;
int mock_modbus_tcp_port = 0;
uint32_t mock_modbus_tcp_ip = 0;
serial_desc_t **mock_modbus_tcp_serial_desc = NULL;
tcp_desc_t **mock_modbus_tcp_tcp_desc = NULL;

int mock_modbus_tcp_deinit_port_called = 0;
unsigned mock_modbus_tcp_deinit_port_indices[MOCK_MODBUS_TCP_MAX_CALLS] = {0};

esp_err_t modbus_tcp_init_port(unsigned index, serial_config_t *config,
                                bridge_mode_t mode, int port, uint32_t ip,
                                serial_desc_t **serial_desc, tcp_desc_t **tcp_desc)
{
    if (mock_modbus_tcp_init_port_called < MOCK_MODBUS_TCP_MAX_CALLS) {
        mock_modbus_tcp_init_port_indices[mock_modbus_tcp_init_port_called] = index;
    }
    mock_modbus_tcp_init_port_called++;
    mock_modbus_tcp_config = config;
    mock_modbus_tcp_mode = mode;
    mock_modbus_tcp_port = port;
    mock_modbus_tcp_ip = ip;
    mock_modbus_tcp_serial_desc = serial_desc;
    mock_modbus_tcp_tcp_desc = tcp_desc;
    return mock_modbus_tcp_init_port_return_value;
}

esp_err_t modbus_tcp_deinit_port(unsigned index)
{
    if (mock_modbus_tcp_deinit_port_called < MOCK_MODBUS_TCP_MAX_CALLS) {
        mock_modbus_tcp_deinit_port_indices[mock_modbus_tcp_deinit_port_called] = index;
    }
    mock_modbus_tcp_deinit_port_called++;
    return ESP_OK;
}

void mock_modbus_tcp_reset(void)
{
    mock_modbus_tcp_init_port_called = 0;
    mock_modbus_tcp_init_port_return_value = ESP_OK;
    memset(mock_modbus_tcp_init_port_indices, 0, sizeof(mock_modbus_tcp_init_port_indices));
    mock_modbus_tcp_config = NULL;
    mock_modbus_tcp_mode = 0;
    mock_modbus_tcp_port = 0;
    mock_modbus_tcp_ip = 0;
    mock_modbus_tcp_serial_desc = NULL;
    mock_modbus_tcp_tcp_desc = NULL;

    mock_modbus_tcp_deinit_port_called = 0;
    memset(mock_modbus_tcp_deinit_port_indices, 0, sizeof(mock_modbus_tcp_deinit_port_indices));
}
