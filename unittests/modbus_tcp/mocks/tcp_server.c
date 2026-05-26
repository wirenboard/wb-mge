#include "tcp_server.h"
#include <string.h>
#include <stdbool.h>

/* Capture state for tcp_server_send (R2). Tests can read these after a send call. */
uint8_t   mock_tcp_send_buf[1024];
size_t    mock_tcp_send_len  = 0;
int       mock_tcp_send_sock = -1;
esp_err_t mock_tcp_send_result = 0;  /* override to simulate send failure */
bool      mock_tcp_send_overflow = false;  /* set when len > sizeof(mock_tcp_send_buf) */

void mock_tcp_server_reset(void)
{
    mock_tcp_send_len      = 0;
    mock_tcp_send_sock     = -1;
    mock_tcp_send_result   = 0;
    mock_tcp_send_overflow = false;
    memset(mock_tcp_send_buf, 0, sizeof(mock_tcp_send_buf));
}

esp_err_t tcp_server_init(int port, tcp_receive_handler_t handler, tcp_desc_t **desc_out)
{
    (void)port; (void)handler; (void)desc_out;
    return 0;
}

esp_err_t tcp_server_send(tcp_desc_t *desc, int client_sock, uint8_t *data, size_t len)
{
    (void)desc;
    mock_tcp_send_sock = client_sock;
    if (len <= sizeof(mock_tcp_send_buf)) {
        memcpy(mock_tcp_send_buf, data, len);
        mock_tcp_send_len = len;
    } else {
        mock_tcp_send_overflow = true;
        mock_tcp_send_len = len;  /* record size even though data is not copied */
    }
    return mock_tcp_send_result;
}

esp_err_t tcp_server_connected(tcp_desc_t *desc)
{
    (void)desc;
    return 0;
}

esp_err_t tcp_server_deinit(tcp_desc_t *desc)
{
    (void)desc;
    return 0;
}
