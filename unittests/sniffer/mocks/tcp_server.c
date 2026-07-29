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

/* Same, for the entry point fast_modbus.c uses to answer a probe. Linked in but never
 * driven by the sniffer tests; the real validation is covered by the tcp_server suite. */
esp_err_t tcp_server_send_to_captured_client(tcp_desc_t *desc, int client_sock,
                                             uint32_t generation, uint8_t *data, size_t len)
{
    (void)generation;
    return tcp_server_send(desc, client_sock, data, len);
}
