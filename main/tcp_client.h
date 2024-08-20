#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef void(*tcpc_receive_handler_t)(uint8_t*, uint8_t);

esp_err_t tcp_client_init(int host_port, char *host_ip, tcpc_receive_handler_t tcpc_receive_handler);
esp_err_t tcp_client_send(uint8_t *data, uint8_t len);
