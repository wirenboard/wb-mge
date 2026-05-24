#include "tcp_server.h"

esp_err_t tcp_server_init(int port, tcp_receive_handler_t handler, tcp_desc_t **desc_out)
{
    (void)port; (void)handler; (void)desc_out;
    return 0;
}

esp_err_t tcp_server_send(tcp_desc_t *desc, int client_sock, uint8_t *data, size_t len)
{
    (void)desc; (void)client_sock; (void)data; (void)len;
    return 0;
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
