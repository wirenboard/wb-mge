#include "bridge/tcp_desc.h"
#include "tcp_server.h"

/* Minimal tcp_server_send stub — returns ESP_OK unconditionally */
esp_err_t tcp_server_send(tcp_desc_t *desc, int client_sock, uint8_t *data, size_t len)
{
    (void)desc;
    (void)client_sock;
    (void)data;
    (void)len;
    return ESP_OK;
}
