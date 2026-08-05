/* Test-shim translation unit.
 *
 * The production module cache_modbus_server.c must not depend on anything under
 * unittests/ (review #60). To reach its static callbacks and file-scope state the
 * shims need to live in the same translation unit as the source, so this file
 * #includes the module and defines the test-only shims here. The module is compiled
 * exactly once — through this file — so it must NOT also appear directly in the
 * test Makefile's source list, or the linker would see duplicate symbols.
 *
 * cache_modbus_server_test_api.h is the single declaration of the shim set; it is
 * included after the module so the definitions below have matching prototypes
 * (-Wmissing-prototypes) and the test TU shares the exact same declarations.
 */

#include "cache_modbus_server.c"

#include "cache_modbus_server_test_api.h"

/* Thin shim exposing the static callback for unit tests. */
void cache_modbus_server_test_process(tcp_desc_t *desc, int client_sock,
                                       uint8_t *data, size_t len)
{
    process_data_from_tcp(desc, client_sock, data, len);
}

/* Thin shim exposing the connection-close hook for unit tests, so tests can
 * model the receiver task releasing a reassembly slot on disconnect. */
void cache_modbus_server_test_close(int client_sock)
{
    on_conn_close(NULL, client_sock);
}

void cache_modbus_server_test_reset(void)
{
    memset(&s_reasm, 0, sizeof(s_reasm));
    for (int i = 0; i < MBTCP_REASM_MAX_CONNS; i++) {
        s_reasm.slots[i].sock = -1;
        s_reasm.slots[i].len  = 0;
    }
    s_reasm.mutex = NULL;   /* no mutex in unit tests: single-threaded */
    s_reasm.tag   = TAG;
    s_tcp_desc    = NULL;
    s_port        = 0;
}

/* Number of recv callbacks that found no free reassembly slot since reset. */
uint32_t cache_modbus_server_test_get_slot_exhausted(void)
{
    return mbtcp_reasm_slot_exhausted(&s_reasm);
}
