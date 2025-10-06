#include "bridge/tcp_server.h"
#include <string.h>

esp_err_t mock_tcp_send_result = ESP_OK;

esp_err_t tcp_server_send(tcp_desc_t *desc, uint8_t *data, size_t len)
{
    (void)desc;
    (void)data;
    (void)len;
    if (mock_tcp_send_result != ESP_OK) {
        return mock_tcp_send_result;
    }
    return ESP_OK;
}
