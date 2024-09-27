#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef void (*tcps_receive_handler_t)(uint8_t *, uint8_t);

esp_err_t tcp_server_init(int port, tcps_receive_handler_t tcps_receive_handler);
esp_err_t tcp_server_send(uint8_t *data, uint8_t len);
