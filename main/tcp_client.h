#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef void (*tcpc_receive_handler_t)(uint8_t *, size_t);

esp_err_t tcp_client_init(uint32_t host_ip, uint16_t host_port,
                          tcpc_receive_handler_t tcpc_receive_handler);
esp_err_t tcp_client_send(uint8_t *data, size_t len);
