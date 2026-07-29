// Mock implementation of tcp_server for transparent_tcp unit tests.
// Tracks call counts, stores arguments, provides a fake tcp_desc_t* on success,
// and can be configured to fail via mock_tcp_server_calls.init_should_fail.

#include "tcp_server.h"
#include "mock_tcp_server.h"
#include "call_sequence.h"
#include <string.h>

static tcp_desc_t mock_tcp_desc;
mock_tcp_server_calls_t mock_tcp_server_calls = {0};
tcp_receive_handler_t mock_tcp_server_registered_handler = 0;

tcp_desc_t *mock_tcp_server_get_desc(void)
{
    return &mock_tcp_desc;
}

void mock_tcp_server_reset(void)
{
    memset(&mock_tcp_server_calls, 0, sizeof(mock_tcp_server_calls));
    memset(&mock_tcp_desc, 0, sizeof(mock_tcp_desc));
    mock_tcp_desc.last_client_sock = -1;   // as the real tcp_server_init() leaves it
    mock_tcp_server_registered_handler = 0;
    mock_tcp_server_calls.connected_ret = ESP_OK;  // default: connected
}

void mock_tcp_server_simulate_client_admitted(int client_sock)
{
    mock_tcp_desc.active_connections = 1;
    mock_tcp_desc.last_client_sock = client_sock;
    mock_tcp_server_calls.connected_ret = ESP_OK;
}

esp_err_t tcp_server_init(int port, tcp_receive_handler_t handler, tcp_desc_t **out_desc)
{
    (void)port;
    mock_tcp_server_calls.init_called++;
    if (mock_tcp_server_calls.init_should_fail) {
        return ESP_FAIL;
    }
    mock_tcp_server_registered_handler = handler;
    mock_tcp_desc.last_client_sock = -1;   // real init sets the "no client" sentinel
    *out_desc = &mock_tcp_desc;
    return ESP_OK;
}

void tcp_server_set_max_connections(tcp_desc_t *desc, uint32_t max_connections)
{
    (void)desc;
    mock_tcp_server_calls.set_max_connections_called++;
    mock_tcp_server_calls.set_max_connections_value = max_connections;
}

// Mirrors the real function: the TARGET IS RESOLVED HERE, from the descriptor, under the
// connection lock — the caller does not get to pass a socket in. Recording
// desc->last_client_sock at call time is therefore what the production code would send to,
// and a caller that had cached a stale fd could not influence it.
esp_err_t tcp_server_send_to_current_client(tcp_desc_t *desc, uint8_t *data, size_t len)
{
    mock_tcp_server_calls.send_called++;
    mock_tcp_server_calls.send_last_client_sock = desc ? desc->last_client_sock : -1;
    mock_tcp_server_calls.send_last_data = data;
    mock_tcp_server_calls.send_last_len = len;
    if (mock_tcp_server_calls.send_last_client_sock < 0) {
        return ESP_FAIL;   // "No client connected", as the real tcp_server_send() reports
    }
    return ESP_OK;
}

esp_err_t tcp_server_connected(tcp_desc_t *desc)
{
    (void)desc;
    mock_tcp_server_calls.connected_called++;
    return mock_tcp_server_calls.connected_ret;
}

esp_err_t tcp_server_deinit(tcp_desc_t *desc)
{
    (void)desc;
    mock_tcp_server_calls.deinit_called++;
    mock_tcp_server_calls.deinit_call_seq = call_sequence_get_call_id();
    return ESP_OK;
}
