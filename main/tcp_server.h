#pragma once

#include "tcp_desc.h"

#include "esp_err.h"
#include <stdint.h>

esp_err_t tcp_server_init(int port, tcp_receive_handler_t tcps_receive_handler, tcp_desc_t **out_desc);
esp_err_t tcp_server_send(tcp_desc_t *desc, uint8_t *data, size_t len);
