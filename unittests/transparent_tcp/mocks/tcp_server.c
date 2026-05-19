// Mock implementation of tcp_server for transparent_tcp unit tests.
// Tracks call counts, stores arguments, provides a fake tcp_desc_t* on success,
// and can be configured to fail via mock_tcp_server_calls.init_should_fail.

#include "tcp_server.h"
#include "mock_tcp_server.h"
#include <string.h>

static tcp_desc_t mock_tcp_desc;
mock_tcp_server_calls_t mock_tcp_server_calls = {0};

void mock_tcp_server_reset(void)
{
    memset(&mock_tcp_server_calls, 0, sizeof(mock_tcp_server_calls));
    memset(&mock_tcp_desc, 0, sizeof(mock_tcp_desc));
}

esp_err_t tcp_server_init(int port, tcp_receive_handler_t handler, tcp_desc_t **out_desc)
{
    (void)port;
    (void)handler;
    mock_tcp_server_calls.init_called++;
    if (mock_tcp_server_calls.init_should_fail) {
        return ESP_FAIL;
    }
    *out_desc = &mock_tcp_desc;
    return ESP_OK;
}

esp_err_t tcp_server_send(tcp_desc_t *desc, int client_sock, uint8_t *data, size_t len)
{
    (void)desc;
    (void)client_sock;
    (void)data;
    (void)len;
    mock_tcp_server_calls.send_called++;
    return ESP_OK;
}

esp_err_t tcp_server_connected(tcp_desc_t *desc)
{
    (void)desc;
    mock_tcp_server_calls.connected_called++;
    return ESP_OK;
}

esp_err_t tcp_server_deinit(tcp_desc_t *desc)
{
    (void)desc;
    mock_tcp_server_calls.deinit_called++;
    return ESP_OK;
}
