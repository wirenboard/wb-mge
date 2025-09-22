#pragma once

#include "modbus_helpers.h"
#include "modbus_tcp.h"

enum fast_modbus_probe_result
{
    FAST_MODBUS_PROBE_SUCCESS,
    FAST_MODBUS_NOT_PROBE,
    FAST_MODBUS_PROBE_MALLOC_FAIL,
    FAST_MODBUS_PROBE_SEND_FAIL
};

enum fast_modbus_probe_result fast_modbus_send_probe_response(mb_tcp_task_ctx_t* ctx, const mb_tcp_header_t* tcp_req_header);
size_t fast_modbus_truncate_ff(uint8_t **data, size_t len);
