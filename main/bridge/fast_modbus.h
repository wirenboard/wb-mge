#pragma once

#include "tcp_desc.h"
#include <stdbool.h>

enum fast_modbus_probe_result
{
    FAST_MODBUS_PROBE_SUCCESS,
    FAST_MODBUS_NOT_PROBE,
    FAST_MODBUS_PROBE_MALLOC_FAIL,
    FAST_MODBUS_PROBE_SEND_FAIL
};

// Check request for Fast Modbus support and send response confirming device support
enum fast_modbus_probe_result fast_modbus_send_probe_response(uint8_t port, tcp_desc_t *tcp_desc, uint8_t *tcp_req_buf);

// Remove 0xFF bytes preceding the packet start in Fast Modbus.
// Data is an in/out parameter that is modified inside the function
size_t fast_modbus_truncate_ff(uint8_t **data, size_t len);
