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
 * @param exception_code_out Set to 0x02 (NOT_FOUND) or 0x0B (STALE) on failure;
 *                           not modified on success.
 * @return Total response byte count on success; 0 on lookup failure.
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
 * @param exception_code_out Set to 0x02 (NOT_FOUND) or 0x0B (STALE) on failure;
 *                           not modified on success.
 * @return Total response byte count on success; 0 on lookup failure.
 */
size_t cache_modbus_server_build_coil_response(
    uint8_t unit_id, uint8_t fc, uint16_t transaction_id,
    uint16_t start_addr, uint16_t count, uint16_t value_timeout_s,
    uint8_t *resp_buf, uint8_t *exception_code_out);
