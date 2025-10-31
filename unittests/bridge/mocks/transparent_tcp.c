#include "transparent_tcp.h"
#include <string.h>

int mock_transparent_tcp_init_port_called = 0;
esp_err_t mock_transparent_tcp_init_port_return_value = ESP_OK;
unsigned mock_transparent_tcp_init_port_indices[MOCK_TRANSPARENT_TCP_MAX_CALLS] = {0};
serial_config_t *mock_transparent_tcp_config = NULL;
bridge_mode_t mock_transparent_tcp_mode;
int mock_transparent_tcp_port = 0;
uint32_t mock_transparent_tcp_ip = 0;
serial_desc_t **mock_transparent_tcp_serial_desc = NULL;
tcp_desc_t **mock_transparent_tcp_tcp_desc = NULL;

int mock_transparent_tcp_deinit_port_called = 0;
esp_err_t mock_transparent_tcp_deinit_port_return_value = ESP_OK;
unsigned mock_transparent_tcp_deinit_port_indices[MOCK_TRANSPARENT_TCP_MAX_CALLS] = {0};

esp_err_t transparent_tcp_init_port(unsigned index, serial_config_t *config,
                                    bridge_mode_t mode, int port, uint32_t ip,
                                    serial_desc_t **serial_desc, tcp_desc_t **tcp_desc)
{
    if (mock_transparent_tcp_init_port_called < MOCK_TRANSPARENT_TCP_MAX_CALLS) {
        mock_transparent_tcp_init_port_indices[mock_transparent_tcp_init_port_called] = index;
    }
    mock_transparent_tcp_init_port_called++;
    mock_transparent_tcp_config = config;
    mock_transparent_tcp_mode = mode;
    mock_transparent_tcp_port = port;
    mock_transparent_tcp_ip = ip;
    mock_transparent_tcp_serial_desc = serial_desc;
    mock_transparent_tcp_tcp_desc = tcp_desc;
    return mock_transparent_tcp_init_port_return_value;
}

esp_err_t transparent_tcp_deinit_port(unsigned index)
{
    if (mock_transparent_tcp_deinit_port_called < MOCK_TRANSPARENT_TCP_MAX_CALLS) {
        mock_transparent_tcp_deinit_port_indices[mock_transparent_tcp_deinit_port_called] = index;
    }
    mock_transparent_tcp_deinit_port_called++;
    return mock_transparent_tcp_deinit_port_return_value;
}

void mock_transparent_tcp_reset(void)
{
    mock_transparent_tcp_init_port_called = 0;
    mock_transparent_tcp_init_port_return_value = ESP_OK;
    memset(mock_transparent_tcp_init_port_indices, 0, sizeof(mock_transparent_tcp_init_port_indices));
    mock_transparent_tcp_config = NULL;
    mock_transparent_tcp_mode = 0;
    mock_transparent_tcp_port = 0;
    mock_transparent_tcp_ip = 0;
    mock_transparent_tcp_serial_desc = NULL;
    mock_transparent_tcp_tcp_desc = NULL;

    mock_transparent_tcp_deinit_port_called = 0;
    mock_transparent_tcp_deinit_port_return_value = ESP_OK;
    memset(mock_transparent_tcp_deinit_port_indices, 0, sizeof(mock_transparent_tcp_deinit_port_indices));
}
