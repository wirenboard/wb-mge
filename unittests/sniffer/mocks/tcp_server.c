#include "bridge/tcp_desc.h"
#include "tcp_server.h"

/* Minimal tcp_server_send stub — returns ESP_OK unconditionally */
esp_err_t tcp_server_send(tcp_desc_t *desc, uint8_t *data, size_t len)
{
    (void)desc;
    (void)data;
    (void)len;
    return ESP_OK;
}
