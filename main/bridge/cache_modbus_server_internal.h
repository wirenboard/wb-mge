#pragma once

/* Internal builder functions for cache_modbus_server.c.
 * Not part of the public API — exposed without static linkage only
 * to allow direct unit testing without a running TCP server. */

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Build an FC03 or FC04 register response into resp_buf.
 *
 * @param unit_id           Modbus unit ID from the request.
 * @param fc                Function code: 0x03 or 0x04.
 * @param transaction_id    Transaction ID in network byte order.
 * @param start_addr        Starting register address (0-based).
 * @param count             Number of registers (1..125).
 * @param value_timeout_s   Passed to cache_multimaster_lookup().
 * @param resp_buf          Output buffer of at least (9 + count*2) bytes.
 * @param exception_code_out Set to 0x02 if the address range overflows 16-bit space
 *                           (start_addr + count > 0x10000), 0x02 if not found in cache,
 *                           or 0x0B if the cached entry is stale; not modified on success.
 * @return Total response byte count on success; 0 on overflow or lookup failure.
 */
size_t cache_modbus_server_build_register_response(
    uint8_t unit_id, uint8_t fc, uint16_t transaction_id,
    uint16_t start_addr, uint16_t count, uint16_t value_timeout_s,
    uint8_t *resp_buf, uint8_t *exception_code_out);

/**
 * @brief Build an FC01 or FC02 coil/discrete-input response into resp_buf.
 *
 * @param unit_id           Modbus unit ID from the request.
 * @param fc                Function code: 0x01 or 0x02.
 * @param transaction_id    Transaction ID in network byte order.
 * @param start_addr        Starting coil address (0-based).
 * @param count             Number of coils (1..2000).
 * @param value_timeout_s   Passed to cache_multimaster_lookup().
 * @param resp_buf          Output buffer of at least (9 + ceil(count/8)) bytes.
 * @param exception_code_out Set to 0x02 if the address range overflows 16-bit space
 *                           (start_addr + count > 0x10000), 0x02 if not found in cache,
 *                           or 0x0B if the cached entry is stale; not modified on success.
 * @return Total response byte count on success; 0 on overflow or lookup failure.
 */
size_t cache_modbus_server_build_coil_response(
    uint8_t unit_id, uint8_t fc, uint16_t transaction_id,
    uint16_t start_addr, uint16_t count, uint16_t value_timeout_s,
    uint8_t *resp_buf, uint8_t *exception_code_out);

#ifdef __unittest_env__
#include "tcp_desc.h"

/**
 * @brief Test entry point for process_data_from_tcp().
 *
 * Allows unit tests to invoke the static callback directly without a running
 * TCP server.  Available only when __unittest_env__ is defined.
 *
 * @param desc        tcp_desc handle (may be NULL in tests — mocked tcp_server_send ignores it).
 * @param client_sock Client socket file descriptor (any value in tests).
 * @param data        Raw request bytes.
 * @param len         Number of bytes in data.
 */
void cache_modbus_server_test_process(tcp_desc_t *desc, int client_sock,
                                       uint8_t *data, size_t len);

/* Reinitializes the per-connection reassembly table. Call from setUp() in
 * unit tests because tests do not call cache_modbus_server_init(). */
void cache_modbus_server_test_reset(void);
#endif /* __unittest_env__ */
