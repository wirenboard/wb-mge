#pragma once

#include "esp_err.h"
#include "bridge/tcp_desc.h"
#include <stdint.h>

typedef struct {
    int called;
    tcp_desc_t* desc;
    int client_sock;
    size_t len;
    esp_err_t result;
} tcp_server_send_mock_t;

extern tcp_server_send_mock_t tcp_server_send_mock;

esp_err_t tcp_server_send(tcp_desc_t *desc, int client_sock, uint8_t *data, size_t len);

void mock_tcp_server_reset(void);
