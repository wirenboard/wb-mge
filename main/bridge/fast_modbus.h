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

enum fast_modbus_probe_result fast_modbus_send_probe_response(
    uint8_t port,
    tcp_desc_t *tcp_desc,
    uint8_t *tcp_req_buf,
    size_t tcp_req_len
);

size_t fast_modbus_truncate_ff(uint8_t **data, size_t len);
