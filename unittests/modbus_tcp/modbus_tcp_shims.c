/* Test-shim translation unit.
 *
 * The production module modbus_tcp.c must not depend on anything under
 * unittests/ (review #36, #39). To reach its static callbacks and file-scope
 * state the shims need to live in the same translation unit as the source, so
 * this file #includes the module and defines the test-only shims here. The module
 * is compiled exactly once — through this file — so it must NOT also appear
 * directly in the test Makefile's source list, or the linker would see duplicate
 * symbols (same pattern as unittests/cache_modbus_server and virtual_io_qemu).
 *
 * modbus_tcp_internal.h is the single declaration of the shim set; it is included
 * after the module so the definitions below have matching prototypes
 * (-Wmissing-prototypes) and the test TU shares the exact same declarations.
 */

#include "modbus_tcp.c"

#include "modbus_tcp_internal.h"


void modbus_tcp_test_init_ctx(unsigned ctx_idx, packet_queue_handle queue, tcp_desc_t *tcp_desc)
{
    mb_tcp_task_ctx_t *ctx = &mb_tcp_task_ctx[ctx_idx];
    memset(ctx, 0, sizeof(*ctx));
    ctx->index       = ctx_idx;
    ctx->tcp_queue   = queue;
    ctx->tcp_desc    = tcp_desc;
    /* No mutex in unit tests: the harness is single-threaded. */
    ctx->reasm.mutex = NULL;
    ctx->reasm.tag   = TAG;
    for (int i = 0; i < MBTCP_REASM_MAX_CONNS; i++) {
        ctx->reasm.slots[i].sock = -1;
        ctx->reasm.slots[i].len  = 0;
    }
}

/* Hand the test the port's reassembler so it can query it through the real
 * mbtcp_reasm API instead of re-exporting each function as its own shim. */
mbtcp_reasm_t *modbus_tcp_test_get_reasm(unsigned ctx_idx)
{
    return &mb_tcp_task_ctx[ctx_idx].reasm;
}

unsigned modbus_tcp_test_push_data(unsigned ctx_idx, int client_sock,
                                    const uint8_t *data, size_t len)
{
    return separate_and_push_requests_from_tcp_with_client(
        &mb_tcp_task_ctx[ctx_idx], client_sock, data, len);
}

void modbus_tcp_test_conn_close(unsigned ctx_idx, int client_sock)
{
    on_tcp_conn_close(mb_tcp_task_ctx[ctx_idx].tcp_desc, client_sock);
}

/* Run the static self-device handler (Unit ID 0xFF dispatch target) for
 * ctx[ctx_idx]. Mirrors the branch taken by modbus_tcp_server_task() when
 * mb_device_is_self() is true: answers locally and sends back to client_sock,
 * never touching the RS485 serial path. */
void modbus_tcp_test_handle_self_device_request(unsigned ctx_idx, int client_sock,
                                                uint8_t *tcp_req_buf, size_t tcp_req_len)
{
    handle_self_device_request(&mb_tcp_task_ctx[ctx_idx], client_sock,
                               tcp_req_buf, tcp_req_len);
}

/* Seed the in-flight RTU request bookkeeping so the on_tcp_conn_close()
 * stale-pending-reset path can be exercised. */
void modbus_tcp_test_set_pending(unsigned ctx_idx, uint16_t tid,
                                 uint8_t slave_id, int client_sock)
{
    mb_tcp_task_ctx_t *ctx = &mb_tcp_task_ctx[ctx_idx];
    ctx->pending_tid         = tid;
    ctx->pending_slave_id    = slave_id;
    ctx->pending_client_sock = client_sock;
}

uint16_t modbus_tcp_test_get_pending_tid(unsigned ctx_idx)
{
    return mb_tcp_task_ctx[ctx_idx].pending_tid;
}

uint8_t modbus_tcp_test_get_pending_slave_id(unsigned ctx_idx)
{
    return mb_tcp_task_ctx[ctx_idx].pending_slave_id;
}

int modbus_tcp_test_get_pending_client_sock(unsigned ctx_idx)
{
    return mb_tcp_task_ctx[ctx_idx].pending_client_sock;
}
