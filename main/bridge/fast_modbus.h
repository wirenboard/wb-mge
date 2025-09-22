#pragma once

#include "modbus_helpers.h"
#include "modbus_tcp.h"
#include <stdint.h>

esp_err_t fast_modbus_send_probe_response(mb_tcp_task_ctx_t* ctx, const mb_tcp_header_t* tcp_req_header);
size_t fast_modbus_truncate_ff(uint8_t** data, size_t len);
