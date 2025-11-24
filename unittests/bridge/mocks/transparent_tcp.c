#include "unity.h"
#include "transparent_tcp.h"
#include <string.h>

mock_transparent_tcp_t mock_transparent_tcp[BRIDGES_COUNT] = {0};
mock_transparent_tcp_calls_t mock_transparent_tcp_calls[BRIDGES_COUNT] = {0};
bool mock_transparent_tcp_init_port_should_fail = false;

static tcp_desc_t mock_tcp_desc[BRIDGES_COUNT];

esp_err_t transparent_tcp_init_port(unsigned index, serial_config_t *config,
                                    bridge_mode_t mode, int port, uint32_t ip,
                                    serial_desc_t **serial_desc, tcp_desc_t **tcp_desc)
{
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(BRIDGES_COUNT, index, "Invalid index");

    mock_transparent_tcp_calls[index].init_port_called++;

    if (mock_transparent_tcp_init_port_should_fail) {
        return ESP_FAIL;
    }

    mock_transparent_tcp[index].config = config;
    mock_transparent_tcp[index].mode = mode;
    mock_transparent_tcp[index].port = port;
    mock_transparent_tcp[index].ip = ip;
    mock_transparent_tcp[index].serial_desc = serial_desc;
    mock_transparent_tcp[index].tcp_desc = tcp_desc;

    *tcp_desc = &mock_tcp_desc[index];

    return ESP_OK;
}

esp_err_t transparent_tcp_deinit_port(unsigned index)
{
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(BRIDGES_COUNT, index, "Invalid index");

    mock_transparent_tcp_calls[index].deinit_port_called++;

    return ESP_OK;
}

void mock_transparent_tcp_reset(void)
{
    memset(mock_transparent_tcp, 0, sizeof(mock_transparent_tcp));
    memset(mock_transparent_tcp_calls, 0, sizeof(mock_transparent_tcp_calls));
    mock_transparent_tcp_init_port_should_fail = false;
    memset(mock_tcp_desc, 0, sizeof(mock_tcp_desc));
}
