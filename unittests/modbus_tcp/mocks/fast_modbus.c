#include "fast_modbus.h"

size_t fast_modbus_truncate_ff(uint8_t **data, size_t len)
{
    /* No-op: return len unchanged (no 0xFF prefix trimming in tests) */
    (void)data;
    return len;
}

enum fast_modbus_probe_result fast_modbus_send_probe_response(
    uint8_t port, tcp_desc_t *tcp_desc, int client_sock, uint8_t *tcp_req_buf)
{
    (void)port; (void)tcp_desc; (void)client_sock; (void)tcp_req_buf;
    return FAST_MODBUS_NOT_PROBE;
}
