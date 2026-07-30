#include "unity.h"
#include "transparent_tcp.h"
#include "modbus_tcp.h"
#include <string.h>

mock_transparent_tcp_t mock_transparent_tcp[BRIDGES_COUNT] = {0};
mock_transparent_tcp_calls_t mock_transparent_tcp_calls[BRIDGES_COUNT] = {0};
bool mock_transparent_tcp_init_port_should_fail = false;
bool mock_transparent_tcp_init_port_should_fail_late = false;

static tcp_desc_t mock_tcp_desc[BRIDGES_COUNT];
static serial_desc_t mock_serial_desc[BRIDGES_COUNT];

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

    *serial_desc = &mock_serial_desc[index];
    *tcp_desc = &mock_tcp_desc[index];

    if (mock_transparent_tcp_init_port_should_fail_late) {
        // Same modelling as the modbus_tcp mock — see the comment there. Deliberately WORSE
        // than the real transparent_tcp_init_port(), which leaves no dangling out-parameter
        // on any path: its one late failure is serial_init(), where *tcp_desc is explicitly
        // NULLed and *serial_desc is the NULL serial_init() just returned. Leaving both set
        // here is the point — it puts the caller-side clear in bridge_port_init() under test
        // on its own, independently of callee behaviour that is not bridge.c's to guarantee.
        mock_tcp_desc[index].active_connections = MOCK_TCP_FAIL_LATE_ACTIVE_CONNS;

        mock_transparent_tcp_calls[index].init_fail_observed_active_conns =
            tcp_server_active_connections((tcp_server_num_t)index);

        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t transparent_tcp_deinit_port(unsigned index)
{
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(BRIDGES_COUNT, index, "Invalid index");

    mock_transparent_tcp_calls[index].deinit_port_called++;

    // Stand-in for the point where the real transparent_tcp_deinit_port() frees the two
    // descriptors — see the modbus_tcp mock for the reasoning.
    mock_transparent_tcp_calls[index].deinit_observed_active_conns =
        tcp_server_active_connections((tcp_server_num_t)index);
    mock_transparent_tcp_calls[index].deinit_observed_serial_desc = bridge_get_serial_desc(index);

    return ESP_OK;
}

void mock_transparent_tcp_reset(void)
{
    memset(mock_transparent_tcp, 0, sizeof(mock_transparent_tcp));
    memset(mock_transparent_tcp_calls, 0, sizeof(mock_transparent_tcp_calls));
    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        // -1 for the same reason as in the modbus_tcp mock: 0 is a meaningful result.
        mock_transparent_tcp_calls[i].init_fail_observed_active_conns = -1;
        mock_transparent_tcp_calls[i].deinit_observed_active_conns = -1;
    }
    mock_transparent_tcp_init_port_should_fail = false;
    mock_transparent_tcp_init_port_should_fail_late = false;
    memset(mock_tcp_desc, 0, sizeof(mock_tcp_desc));
    memset(mock_serial_desc, 0, sizeof(mock_serial_desc));
}
