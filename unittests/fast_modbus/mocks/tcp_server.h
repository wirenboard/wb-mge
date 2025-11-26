#pragma once

#include "esp_err.h"
#include "bridge/tcp_desc.h"
#include <stdint.h>

extern esp_err_t mock_tcp_send_result;

esp_err_t tcp_server_send(tcp_desc_t *desc, uint8_t *data, size_t len);
