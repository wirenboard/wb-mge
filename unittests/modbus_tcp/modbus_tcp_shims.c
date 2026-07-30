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

#include "unity.h"


/* Per-port stand-in serial descriptors, used ONLY as lookup keys.
 *
 * serial_init() hands back mock_serial_init_return, which is NULL unless a test sets it, so
 * a context set up by these shims used to be left with serial_desc == NULL.
 * process_data_from_serial() resolves its context with find_ctx_by_serial_desc(), which
 * scans for the first context whose serial_desc matches — and NULL matched context 0. That
 * happened to be TEST_CTX_IDX, so the shim appeared to work while in fact ignoring its
 * ctx_idx argument entirely: pointing the tests at any other port would have silently
 * driven port 0 instead. (The lookup now rejects a NULL needle outright, so the same
 * mistake would produce no match at all rather than the wrong port — but a distinct key is
 * still what makes the lookup EXACT rather than merely non-NULL.)
 *
 * A distinct non-NULL key per port makes the lookup exact for every ctx_idx. Nothing ever
 * dereferences these: process_data_from_serial() uses the pointer only to find its context,
 * and no shim calls serial_send()/serial_deinit() on one. */
static serial_desc_t test_serial_desc[MODBUS_TCP_MAX_TASK_COUNT];


void modbus_tcp_test_init_ctx(unsigned ctx_idx, packet_queue_handle queue, tcp_desc_t *tcp_desc)
{
    mb_tcp_task_ctx_t *ctx = &mb_tcp_task_ctx[ctx_idx];
    memset(ctx, 0, sizeof(*ctx));
    ctx->index       = ctx_idx;
    ctx->tcp_queue   = queue;
    ctx->tcp_desc    = tcp_desc;
    /* Unique lookup key for this port, so find_ctx_by_serial_desc() resolves to exactly
     * this context rather than to whichever one happens to hold NULL — see above. */
    ctx->serial_desc = &test_serial_desc[ctx_idx];
    /* As a running port would be: initialized, with an event group the RS-485 receive
     * path can signal EVENT_SERIAL_RESPONSE_RECEIVED on. */
    ctx->initialized = true;
    ctx->event_group = (EventGroupHandle_t)0xDEADBEEF;
    /* No mutex in unit tests: the harness is single-threaded. */
    ctx->reasm.mutex = NULL;
    ctx->reasm.tag   = TAG;
    for (int i = 0; i < MBTCP_REASM_MAX_CONNS; i++) {
        ctx->reasm.slots[i].sock = -1;
        ctx->reasm.slots[i].len  = 0;
    }
}

void modbus_tcp_test_reset_all_ctx(void)
{
    memset(mb_tcp_task_ctx, 0, sizeof(mb_tcp_task_ctx));
}

int modbus_tcp_test_find_ctx_idx_by_serial_desc(const serial_desc_t *serial_desc)
{
    const mb_tcp_task_ctx_t *ctx = find_ctx_by_serial_desc(serial_desc);
    return (ctx == NULL) ? -1 : (int)(ctx - mb_tcp_task_ctx);
}

int modbus_tcp_test_find_ctx_idx_by_tcp_desc(const tcp_desc_t *tcp_desc)
{
    const mb_tcp_task_ctx_t *ctx = find_ctx_by_tcp_desc(tcp_desc);
    return (ctx == NULL) ? -1 : (int)(ctx - mb_tcp_task_ctx);
}

serial_desc_t *modbus_tcp_test_get_ctx_serial_desc(unsigned ctx_idx)
{
    return mb_tcp_task_ctx[ctx_idx].serial_desc;
}

tcp_desc_t *modbus_tcp_test_get_ctx_tcp_desc(unsigned ctx_idx)
{
    return mb_tcp_task_ctx[ctx_idx].tcp_desc;
}

packet_queue_handle modbus_tcp_test_get_ctx_queue(unsigned ctx_idx)
{
    return mb_tcp_task_ctx[ctx_idx].tcp_queue;
}

EventGroupHandle_t modbus_tcp_test_get_ctx_event_group(unsigned ctx_idx)
{
    return mb_tcp_task_ctx[ctx_idx].event_group;
}

bool modbus_tcp_test_get_ctx_initialized(unsigned ctx_idx)
{
    return mb_tcp_task_ctx[ctx_idx].initialized;
}

void modbus_tcp_test_set_ctx_initialized(unsigned ctx_idx, bool initialized)
{
    mb_tcp_task_ctx[ctx_idx].initialized = initialized;
}

/* Enter through the production receive handler with only a descriptor in hand — the same
 * call tcp_server makes — so the context is resolved by find_ctx_by_tcp_desc() and not by
 * the test. */
void modbus_tcp_test_deliver_tcp_data(tcp_desc_t *desc, int client_sock,
                                      uint8_t *data, size_t len)
{
    process_data_from_tcp(desc, client_sock, data, len);
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
                                                uint32_t conn_generation,
                                                uint8_t *tcp_req_buf, size_t tcp_req_len)
{
    handle_self_device_request(&mb_tcp_task_ctx[ctx_idx], client_sock, conn_generation,
                               tcp_req_buf, tcp_req_len);
}

/* Drive the production dequeue path (pop + adopt the request's identity) without the
 * surrounding task loop. */
size_t modbus_tcp_test_fetch_tcp_request(unsigned ctx_idx, uint8_t **tcp_req_buf,
                                         int *client_sock, uint32_t *conn_generation)
{
    return fetch_tcp_request(&mb_tcp_task_ctx[ctx_idx], tcp_req_buf, client_sock, conn_generation);
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

void modbus_tcp_test_set_pending_generation(unsigned ctx_idx, uint32_t generation)
{
    mb_tcp_task_ctx[ctx_idx].pending_conn_generation = generation;
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

uint32_t modbus_tcp_test_get_pending_generation(unsigned ctx_idx)
{
    return mb_tcp_task_ctx[ctx_idx].pending_conn_generation;
}


/* Deliver an RTU response to the port exactly as the UART event task does — through the
 * production entry point, so the context resolution under test is the real one.
 *
 * The guard makes the shim's one hidden dependency explicit: the callback finds its context
 * by looking the descriptor up, so a test only drives the port it asked for as long as the
 * key is unique to that port (see test_serial_desc above). Without it, a change to the key
 * scheme would silently redirect every test to whichever context matched first. */
void modbus_tcp_test_process_data_from_serial(unsigned ctx_idx, uint8_t *data, size_t len)
{
    serial_desc_t *desc = mb_tcp_task_ctx[ctx_idx].serial_desc;
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&mb_tcp_task_ctx[ctx_idx], find_ctx_by_serial_desc(desc),
        "serial_desc must resolve to the requested context, not merely to context 0");
    process_data_from_serial(desc, data, len);
}
