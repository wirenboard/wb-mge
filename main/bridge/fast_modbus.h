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

// Check request for Fast Modbus support and send response confirming device support.
// The probe arrives through the Modbus TCP packet queue and is answered from
// modbus_tcp_server_task(), not from the connection's receiver task, so the caller must
// pass the (client_sock, conn_generation) pair that travelled with the request: the reply
// is sent through tcp_server_send_to_captured_client(), which drops it if that connection
// is gone rather than writing into whatever socket inherited the fd number.
enum fast_modbus_probe_result fast_modbus_send_probe_response(uint8_t port, tcp_desc_t *tcp_desc, int client_sock,
                                                              uint32_t conn_generation, uint8_t *tcp_req_buf);

// Remove 0xFF bytes preceding the packet start in Fast Modbus.
// Data is an in/out parameter that is modified inside the function
size_t fast_modbus_truncate_ff(uint8_t **data, size_t len);
