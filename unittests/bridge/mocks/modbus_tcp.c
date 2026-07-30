#include "unity.h"
#include "modbus_tcp.h"
#include <string.h>

mock_modbus_tcp_t mock_modbus_tcp[BRIDGES_COUNT] = {0};
mock_modbus_tcp_calls_t mock_modbus_tcp_calls[BRIDGES_COUNT] = {0};
bool mock_modbus_tcp_init_port_should_fail = false;
bool mock_modbus_tcp_init_port_should_fail_late = false;

static tcp_desc_t mock_tcp_desc[BRIDGES_COUNT];
static serial_desc_t mock_serial_desc[BRIDGES_COUNT];

esp_err_t modbus_tcp_init_port(unsigned index, serial_config_t *config,
                                bridge_mode_t mode, int port, uint32_t ip,
                                serial_desc_t **serial_desc, tcp_desc_t **tcp_desc)
{
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(BRIDGES_COUNT, index, "modbus_tcp_init_port called with invalid index");

    mock_modbus_tcp_calls[index].init_port_called++;

    if (mock_modbus_tcp_init_port_should_fail) {
        return ESP_FAIL;
    }

    mock_modbus_tcp[index].config = config;
    mock_modbus_tcp[index].mode = mode;
    mock_modbus_tcp[index].port = port;
    mock_modbus_tcp[index].ip = ip;
    mock_modbus_tcp[index].serial_desc = serial_desc;
    mock_modbus_tcp[index].tcp_desc = tcp_desc;

    *serial_desc = &mock_serial_desc[index];
    *tcp_desc = &mock_tcp_desc[index];

    if (mock_modbus_tcp_init_port_should_fail_late) {
        // Model the real module's LATE failure paths (modbus_tcp.c): a descriptor has
        // already been created and written to the caller's out-parameter, then something
        // fails and the cleanup frees it again. BOTH descriptors exist only on the
        // task-creation branch; on the packet-queue and tcp_server_init() branches
        // tcp_server_init() has not succeeded, so *tcp_desc was never written there at all.
        // This mock takes the widest case and, unlike the real module, clears NEITHER
        // out-parameter — the caller-side clear in bridge_port_init() has to hold whatever
        // the callee does.
        mock_tcp_desc[index].active_connections = MOCK_TCP_FAIL_LATE_ACTIVE_CONNS;

        // Sampled at the instant the module would free the descriptors, i.e. inside the
        // window a GET /info on the httpd task can fall into. bridge.c has not returned
        // from this call yet, so it cannot have cleared anything: the context still points
        // at the about-to-be-freed descriptor and bridge_current_cfg[index].bridge_mode
        // still says SERVER/CLIENT. Only a guard that consults `initialized` reports 0.
        mock_modbus_tcp_calls[index].init_fail_observed_active_conns =
            tcp_server_active_connections((tcp_server_num_t)index);

        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t modbus_tcp_deinit_port(unsigned index)
{
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(BRIDGES_COUNT, index, "modbus_tcp_deinit_port called with invalid index");

    mock_modbus_tcp_calls[index].deinit_port_called++;

    // Stand-in for the point where the real modbus_tcp_deinit_port() frees the two
    // descriptors (serial_deinit() / tcp_server_deinit()). Anything bridge.c can still
    // reach from here is a pointer to memory that is about to go away, so the tests assert
    // that both readers already come up empty by the time we get here.
    mock_modbus_tcp_calls[index].deinit_observed_active_conns =
        tcp_server_active_connections((tcp_server_num_t)index);
    mock_modbus_tcp_calls[index].deinit_observed_serial_desc = bridge_get_serial_desc(index);

    return ESP_OK;
}

void mock_modbus_tcp_reset(void)
{
    memset(mock_modbus_tcp, 0, sizeof(mock_modbus_tcp));
    memset(mock_modbus_tcp_calls, 0, sizeof(mock_modbus_tcp_calls));
    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        // -1, not 0: 0 is the value the tests demand at the observation points, so a
        // zeroed field would look like a passing observation that never happened.
        mock_modbus_tcp_calls[i].init_fail_observed_active_conns = -1;
        mock_modbus_tcp_calls[i].deinit_observed_active_conns = -1;
    }
    mock_modbus_tcp_init_port_should_fail = false;
    mock_modbus_tcp_init_port_should_fail_late = false;
    memset(mock_tcp_desc, 0, sizeof(mock_tcp_desc));
    memset(mock_serial_desc, 0, sizeof(mock_serial_desc));
}
