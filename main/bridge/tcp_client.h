#pragma once

#include "tcp_desc.h"

#include "esp_err.h"
#include <stdint.h>

esp_err_t tcp_client_init(uint32_t host_ip, uint16_t host_port,
                          tcp_receive_handler_t tcpc_receive_handler, tcp_desc_t **out_desc);
esp_err_t tcp_client_send(tcp_desc_t *desc, int client_sock, uint8_t *data, size_t len);
esp_err_t tcp_client_connected(tcp_desc_t *desc);

esp_err_t tcp_client_deinit(tcp_desc_t *desc);
