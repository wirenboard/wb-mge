#include "bridge/tcp_desc.h"
#include "tcp_server.h"

#include <string.h>

tcp_server_send_mock_t tcp_server_send_mock = {0};

esp_err_t tcp_server_send(tcp_desc_t *desc, int client_sock, uint8_t *data, size_t len)
{
    tcp_server_send_mock.called++;
    tcp_server_send_mock.desc = desc;
    tcp_server_send_mock.client_sock = client_sock;
    tcp_server_send_mock.len = len;

    /* Save a copy of the payload so tests can inspect it after free() */
    if (data && (len <= sizeof(tcp_server_send_mock.last_data))) {
        memcpy(tcp_server_send_mock.last_data, data, len);
    }

    return tcp_server_send_mock.result;
}

void mock_tcp_server_reset(void)
{
    memset(&tcp_server_send_mock, 0, sizeof(tcp_server_send_mock_t));
}
