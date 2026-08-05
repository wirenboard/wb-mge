#pragma once

/* Test-only hooks into cache_modbus_server.
 *
 * These are NOT production interfaces: every one of them is compiled only under
 * __unittest_env__ and exists solely so the tests can drive the module's static
 * callbacks without a running TCP server. They used to sit in the module's
 * private header inside main/bridge/, mixed in with the production builder
 * prototypes (review #60); they live here now.
 *
 * This header is the single declaration of the whole set — the test file must not
 * hand-declare any of them. (It used to, which is how cache_modbus_server_test_close()
 * and cache_modbus_server_test_get_slot_exhausted() ended up implemented but never
 * declared in the header at all.)
 *
 * cache_modbus_server_shims.c (which #includes cache_modbus_server.c to reach
 * its statics) includes this file so the shim definitions have prototypes
 * (-Wmissing-prototypes). The production module itself no longer references it.
 */

#ifdef __unittest_env__

#include "tcp_desc.h"

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Drive the static TCP receive callback (process_data_from_tcp).
 *
 * @param desc        tcp_desc handle; may be NULL in tests — the mocked
 *                    tcp_server_send() ignores it.
 * @param client_sock Client socket file descriptor (any value in tests).
 * @param data        Raw bytes as they would arrive from recv().
 * @param len         Number of bytes in data.
 */
void cache_modbus_server_test_process(tcp_desc_t *desc, int client_sock,
                                      uint8_t *data, size_t len);

/* Drive the static connection-close hook, so a test can model the receiver task
 * releasing a reassembly slot on disconnect. */
void cache_modbus_server_test_close(int client_sock);

/* Reset all module state (reassembler slots, listener handle, port).
 * Call from setUp(): tests never call cache_modbus_server_init(). */
void cache_modbus_server_test_reset(void);

/* Number of recv callbacks that found no free reassembly slot since the last
 * reset — i.e. more simultaneous connections than the reassembler can track. */
uint32_t cache_modbus_server_test_get_slot_exhausted(void);

#endif /* __unittest_env__ */
