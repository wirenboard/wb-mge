#include "tcp_server.h"
#include <string.h>
#include <stdint.h>

#define MOCK_TCP_SEND_BUF_SIZE 512

/* ---- Captured data from the most recent tcp_server_send() call ----------- */

uint8_t mock_tcp_send_buf[MOCK_TCP_SEND_BUF_SIZE];
size_t  mock_tcp_send_len    = 0;
int     mock_tcp_send_called = 0;

/* ---- init/deinit call tracking (persist-1) ------------------------------- */

int mock_tcp_server_init_called   = 0;
int mock_tcp_server_deinit_called = 0;
int mock_tcp_server_last_port     = 0;
static tcp_desc_t mock_desc;  /* backing storage handed back via *desc_out */

/* ---- Mock implementations ------------------------------------------------ */

esp_err_t tcp_server_send(tcp_desc_t *desc, int client_sock, uint8_t *data, size_t len)
{
    (void)desc;
    (void)client_sock;
    mock_tcp_send_called++;
    if (len > MOCK_TCP_SEND_BUF_SIZE) {
        len = MOCK_TCP_SEND_BUF_SIZE;
    }
    memcpy(mock_tcp_send_buf, data, len);
    mock_tcp_send_len = len;
    return 0; /* ESP_OK */
}

esp_err_t tcp_server_init(int port, tcp_receive_handler_t handler, tcp_desc_t **desc_out)
{
    mock_tcp_server_init_called++;
    mock_tcp_server_last_port = port;
    mock_desc.port           = port;
    mock_desc.close_handler  = NULL;
    if (desc_out != NULL) {
        *desc_out = &mock_desc;
    }
    return 0; /* ESP_OK */
}

esp_err_t tcp_server_deinit(tcp_desc_t *desc)
{
    (void)desc;
    mock_tcp_server_deinit_called++;
    return 0; /* ESP_OK */
}

esp_err_t tcp_server_connected(tcp_desc_t *desc)
{
    (void)desc;
    return 0; /* ESP_OK */
}

/* ---- Reset helper -------------------------------------------------------- */

void mock_tcp_server_reset(void)
{
    memset(mock_tcp_send_buf, 0, sizeof(mock_tcp_send_buf));
    mock_tcp_send_len    = 0;
    mock_tcp_send_called = 0;
    mock_tcp_server_init_called   = 0;
    mock_tcp_server_deinit_called = 0;
    mock_tcp_server_last_port     = 0;
}
