#include "unity.h"
#include "modbus_tcp.h"
#include <string.h>

mock_modbus_tcp_t mock_modbus_tcp[BRIDGES_COUNT] = {0};
mock_modbus_tcp_calls_t mock_modbus_tcp_calls[BRIDGES_COUNT] = {0};

static tcp_desc_t mock_tcp_desc[BRIDGES_COUNT];

esp_err_t modbus_tcp_init_port(unsigned index, serial_config_t *config,
                                bridge_mode_t mode, int port, uint32_t ip,
                                serial_desc_t **serial_desc, tcp_desc_t **tcp_desc)
{
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(BRIDGES_COUNT, index, "modbus_tcp_init_port called with invalid index");

    mock_modbus_tcp_calls[index].init_port_called++;

    if (mode != BRIDGE_MODE_SERVER) {
        return ESP_ERR_INVALID_ARG;
    }

    mock_modbus_tcp[index].config = config;
    mock_modbus_tcp[index].mode = mode;
    mock_modbus_tcp[index].port = port;
    mock_modbus_tcp[index].ip = ip;
    mock_modbus_tcp[index].serial_desc = serial_desc;
    mock_modbus_tcp[index].tcp_desc = tcp_desc;

    *tcp_desc = &mock_tcp_desc[index];

    return ESP_OK;
}

esp_err_t modbus_tcp_deinit_port(unsigned index)
{
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(BRIDGES_COUNT, index, "modbus_tcp_deinit_port called with invalid index");

    mock_modbus_tcp_calls[index].deinit_port_called++;

    return ESP_OK;
}

void mock_modbus_tcp_reset(void)
{
    memset(mock_modbus_tcp, 0, sizeof(mock_modbus_tcp));
    memset(mock_modbus_tcp_calls, 0, sizeof(mock_modbus_tcp_calls));
    memset(mock_tcp_desc, 0, sizeof(mock_tcp_desc));
}
