#pragma once

/* Private interface of the cache Modbus server module.
 *
 * The response builders are production code — process_one_frame() dispatches
 * through them — but they are kept non-static so they can also be driven
 * directly, without a running TCP server. This is NOT the public API
 * (cache_modbus_server.h); nothing outside the module should include it.
 *
 * The __unittest_env__ shims that used to live here now sit with the tests, in
 * unittests/cache_modbus_server/cache_modbus_server_test_api.h. */

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

/**
 * @brief Common signature of the two response builders above.
 *
 * The register and coil builders are interchangeable at the call site: the
 * request dispatcher picks one by function code and invokes it through this
 * pointer instead of duplicating the call and its error handling per branch.
 */
typedef size_t (*cache_mb_response_builder_t)(
    uint8_t unit_id, uint8_t fc, uint16_t transaction_id,
    uint16_t start_addr, uint16_t count, uint16_t value_timeout_s,
    uint8_t *resp_buf, uint8_t *exception_code_out);
