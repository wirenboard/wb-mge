#include "unity.h"

#include "tcp_server.h"
#include "tcp_desc.h"
#include "lwip/sockets.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "malloc.h"

#include <string.h>
#include <stdlib.h>

/* ── Mock state from lwip/sockets.c ────────────────────────────────────── */
extern int  mock_recv_return_values[MOCK_RECV_MAX_VALUES];
extern int  mock_recv_return_count;
extern int  mock_recv_call_count;
extern uint8_t mock_recv_data[MOCK_RECV_DATA_SIZE];
extern int  mock_recv_data_len;
extern int  mock_socket_fd;
extern bool mock_socket_should_fail;
extern bool mock_bind_should_fail;
extern bool mock_listen_should_fail;
extern int  mock_socket_last_domain;
extern int  mock_socket_last_type;
extern int  mock_socket_last_protocol;
extern int  mock_bind_call_count;
extern struct sockaddr_storage mock_bind_last_addr;
extern socklen_t mock_bind_last_addrlen;
extern int  mock_accept_fd;
extern int  mock_accept_call_count;
extern int  mock_accept_fail_count;
extern int  mock_accept_errno;
extern int  mock_close_call_count;
extern int  mock_shutdown_call_count;
extern int  mock_send_call_count;
extern int  mock_send_last_fd;
void mock_lwip_sockets_reset(void);

/* ── Mock state from freertos mocks ────────────────────────────────────── */
extern mock_xEventGroupCreate_t mock_xEventGroupCreate_data;
extern mock_xEventGroupWaitBits_t mock_xEventGroupWaitBits_data;
extern mock_vEventGroupDelete_t mock_vEventGroupDelete_data;
extern mock_xTaskCreate_t mock_xTaskCreate_data;
void mock_freertos_event_groups_reset(void);
void mock_freertos_task_reset(void);

/* ── Per-test callback tracking ─────────────────────────────────────────── */

static int g_receive_handler_called = 0;
static int g_receive_handler_sock = -1;
static size_t g_receive_handler_len = 0;

static int g_close_handler_called = 0;
static int g_close_handler_sock = -1;

static void stub_receive_handler(tcp_desc_t *desc, int client_sock, uint8_t *data, size_t len)
{
    (void)desc;
    (void)data;
    g_receive_handler_called++;
    g_receive_handler_sock = client_sock;
    g_receive_handler_len = len;
}

static void stub_close_handler(tcp_desc_t *desc, int client_sock)
{
    (void)desc;
    g_close_handler_called++;
    g_close_handler_sock = client_sock;
}

/* ── setUp / tearDown ───────────────────────────────────────────────────── */

void setUp(void)
{
    mock_lwip_sockets_reset();
    mock_freertos_event_groups_reset();
    mock_freertos_task_reset();
    mock_freertos_semaphore_reset();
    reset_malloc_tracking();

    g_receive_handler_called = 0;
    g_receive_handler_sock = -1;
    g_receive_handler_len = 0;
    g_close_handler_called = 0;
    g_close_handler_sock = -1;
}

void tearDown(void)
{
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Section 1: receiver logic via tcp_server_run_receiver_for_test()
 * ═══════════════════════════════════════════════════════════════════════════ */

/* close_handler called when recv() returns 0 (connection closed) */
void test_receiver_calls_close_handler_on_disconnect(void)
{
    tcp_desc_t desc = {0};
    desc.receive_handler = stub_receive_handler;
    desc.close_handler = stub_close_handler;
    desc.active_connections = 1;
    desc.port = 502;

    /* recv returns 0 immediately — connection closed */
    mock_recv_return_values[0] = 0;
    mock_recv_return_count = 1;

    tcp_server_run_receiver_for_test(&desc, 10);

    TEST_ASSERT_EQUAL(1, g_close_handler_called);
    TEST_ASSERT_EQUAL(10, g_close_handler_sock);
    TEST_ASSERT_EQUAL(0, desc.active_connections);
}

/* close_handler NOT called when desc->close_handler == NULL */
void test_receiver_no_close_handler_when_null(void)
{
    tcp_desc_t desc = {0};
    desc.receive_handler = stub_receive_handler;
    desc.close_handler = NULL;
    desc.active_connections = 1;
    desc.port = 502;

    mock_recv_return_values[0] = 0;
    mock_recv_return_count = 1;

    /* Must not crash even with NULL close_handler */
    tcp_server_run_receiver_for_test(&desc, 10);

    TEST_ASSERT_EQUAL(0, g_close_handler_called);
    TEST_ASSERT_EQUAL(0, desc.active_connections);
}

/* receive_handler called when data received */
void test_receiver_calls_receive_handler_with_data(void)
{
    tcp_desc_t desc = {0};
    desc.receive_handler = stub_receive_handler;
    desc.close_handler = stub_close_handler;
    desc.active_connections = 1;
    desc.port = 502;
    /* Non-NULL handle: run_receiver checks the exit flag after each packet via
     * check_task_exit_req(); the mock returns 0 (no exit) so the loop continues. */
    desc.event_group = (EventGroupHandle_t)0xDEADBEEF;

    /* Set data bytes in the buffer */
    mock_recv_data[0] = 0xAB;
    mock_recv_data[1] = 0xCD;
    mock_recv_data_len = 2;

    /* First recv returns 2 bytes, second returns 0 (disconnect) */
    mock_recv_return_values[0] = 2;
    mock_recv_return_values[1] = 0;
    mock_recv_return_count = 2;

    tcp_server_run_receiver_for_test(&desc, 10);

    TEST_ASSERT_EQUAL(1, g_receive_handler_called);
    TEST_ASSERT_EQUAL(10, g_receive_handler_sock);
    TEST_ASSERT_EQUAL(2, g_receive_handler_len);
}

/* active_connections decremented after task completes */
void test_receiver_decrements_active_connections(void)
{
    tcp_desc_t desc = {0};
    desc.receive_handler = stub_receive_handler;
    desc.close_handler = NULL;
    desc.active_connections = 3;
    desc.port = 502;

    mock_recv_return_values[0] = 0;
    mock_recv_return_count = 1;

    tcp_server_run_receiver_for_test(&desc, 10);

    TEST_ASSERT_EQUAL(2, desc.active_connections);
}

/* Both receive_handler and close_handler called in correct order */
void test_receiver_sequence_data_then_disconnect(void)
{
    tcp_desc_t desc = {0};
    desc.receive_handler = stub_receive_handler;
    desc.close_handler = stub_close_handler;
    desc.active_connections = 1;
    desc.port = 502;
    /* Non-NULL handle: run_receiver checks the exit flag after each packet via
     * check_task_exit_req(); the mock returns 0 (no exit) so the loop continues. */
    desc.event_group = (EventGroupHandle_t)0xDEADBEEF;

    mock_recv_data[0] = 0x01;
    mock_recv_data_len = 1;

    /* Two data chunks, then disconnect */
    mock_recv_return_values[0] = 1;
    mock_recv_return_values[1] = 1;
    mock_recv_return_values[2] = 0;
    mock_recv_return_count = 3;

    tcp_server_run_receiver_for_test(&desc, 10);

    TEST_ASSERT_EQUAL(2, g_receive_handler_called);
    TEST_ASSERT_EQUAL(1, g_close_handler_called);
    TEST_ASSERT_EQUAL(0, desc.active_connections);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Section 2: tcp_server_init()
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Init succeeds with valid parameters */
void test_tcp_server_init_success(void)
{
    tcp_desc_t *desc = NULL;

    /* socket() returns fd 5, accept returns fd 10 then -1 (to stop acceptor),
     * event group create returns 0xDEADBEEF (already set by reset),
     * xTaskCreate succeeds (already set by reset). */

    esp_err_t ret = tcp_server_init(502, stub_receive_handler, &desc);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_NULL(desc);
    TEST_ASSERT_EQUAL(5, desc->listen_sock);
    TEST_ASSERT_EQUAL(502, desc->port);
    TEST_ASSERT_NOT_NULL(desc->event_group);
    TEST_ASSERT_EQUAL(stub_receive_handler, desc->receive_handler);
    TEST_ASSERT_EQUAL(1, mock_xTaskCreate_data.called);

    free(desc);
}

/* Init fails when socket() fails */
void test_tcp_server_init_socket_fail(void)
{
    tcp_desc_t *desc = NULL;
    mock_socket_should_fail = true;

    esp_err_t ret = tcp_server_init(502, stub_receive_handler, &desc);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NULL(desc);
}

/* Init fails when bind() fails */
void test_tcp_server_init_bind_fail(void)
{
    tcp_desc_t *desc = NULL;
    mock_bind_should_fail = true;

    esp_err_t ret = tcp_server_init(502, stub_receive_handler, &desc);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NULL(desc);
}

/* Init fails when event group creation fails */
void test_tcp_server_init_event_group_fail(void)
{
    tcp_desc_t *desc = NULL;
    mock_xEventGroupCreate_data.should_fail = true;

    esp_err_t ret = tcp_server_init(502, stub_receive_handler, &desc);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NULL(desc);
}

/* Init fails when task creation fails */
void test_tcp_server_init_task_create_fail(void)
{
    tcp_desc_t *desc = NULL;
    mock_xTaskCreate_data.should_fail = true;

    esp_err_t ret = tcp_server_init(502, stub_receive_handler, &desc);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NULL(desc);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Section 3: accept() resource-exhaustion error handling
 * ═══════════════════════════════════════════════════════════════════════════ */

/* When accept() fails with ENFILE (socket table full), the acceptor must NOT
 * close the listen socket — it must just wait and retry.  Closing the listen
 * socket would RST any pending connections in the backlog, causing data loss
 * for other clients.
 *
 * Scenario:
 *   1. accept() call #1 → ENFILE  (mock_accept_fail_count = 1)
 *   2. Acceptor retries.
 *   3. xEventGroupWaitBits sees EVENT_TASK_EXIT_REQ on call #3 → acceptor exits.
 *   4. Acceptor closes the listen socket exactly once (clean shutdown).
 *
 * Expected: mock_close_call_count == 1 (final clean-up only, no intermediate close).
 * With the old (buggy) code the count would be 2 (one on ENFILE + one at exit). */
void test_acceptor_enfile_does_not_close_listen_socket(void)
{
    /* Simulate 1 ENFILE failure.  After the failure the acceptor retries.
     * EXIT_REQ fires on the 3rd xEventGroupWaitBits call so the task exits
     * before the second accept() completes:
     *
     *  New code (fixed):
     *    WaitBits #1: check at top of while(1), 1st iteration  → 0
     *    accept()  #1: → ENFILE
     *    WaitBits #2: check inside ENFILE branch               → 0
     *    vTaskDelay(100 ms), continue
     *    WaitBits #3: check at top of while(1), 2nd iteration  → EXIT_REQ → break
     *    close(listen_sock)                                     ← close #1  (final)
     *    → mock_close_call_count == 1
     *
     *  Old code (buggy):
     *    WaitBits #1: check at top of while(1), 1st iteration  → 0
     *    accept()  #1: → ENFILE
     *    WaitBits #2: check inside else branch                  → 0
     *    vTaskDelay(1000 ms)
     *    close(listen_sock)                                     ← close #1  (spurious!)
     *    create_listen_socket()
     *    continue
     *    WaitBits #3: check at top of while(1), 2nd iteration  → EXIT_REQ → break
     *    close(listen_sock)                                     ← close #2  (final)
     *    → mock_close_call_count == 2
     */
    mock_accept_fail_count = 1;
    mock_accept_errno      = ENFILE;
    /* EXIT_REQ fires before the second accept() — this fd is never dispensed
     * because we do NOT use self_execution (no receiver_task is created). */
    mock_accept_fd = 10;

    /* Trigger EXIT_REQ starting from the 3rd xEventGroupWaitBits call.
     * Without self_execution the xTaskCreate mock records pvTaskCode/pvParameters
     * but does NOT call the task; we invoke the acceptor synchronously below.
     * EVENT_TASK_EXIT_REQ is defined as BIT8 = (1 << 8) in tcp_server.c. */
    mock_xEventGroupWaitBits_data.set_event_on_call = 2;       /* trigger after 2nd call */
    mock_xEventGroupWaitBits_data.events_to_set     = (1 << 8); /* EVENT_TASK_EXIT_REQ = BIT8 */

    /* Init creates the desc and records the task function in the xTaskCreate mock. */
    tcp_desc_t *desc = NULL;
    esp_err_t ret = tcp_server_init(502, stub_receive_handler, &desc);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_NULL(desc);

    /* Invoke the acceptor task synchronously (no self_execution to avoid the
     * receiver_task being spawned, which would skew the close-call count). */
    TEST_ASSERT_NOT_NULL(mock_xTaskCreate_data.pvTaskCode);
    mock_xTaskCreate_data.pvTaskCode(mock_xTaskCreate_data.pvParameters);

    /* The listen socket must have been closed exactly once — during the clean-
     * shutdown path at the end of tcp_server_task(), NOT inside the ENFILE handler. */
    TEST_ASSERT_EQUAL(1, mock_accept_call_count);   /* exactly 1 accept() attempted */
    TEST_ASSERT_EQUAL(1, mock_close_call_count);    /* exactly 1 close() — final shutdown only */
    /* Plain invariant of the exit path: the descriptor outlives the acceptor, and
     * tcp_server_deinit() claims the very same field — before or after this exit path, in an
     * order nothing fixes — so it must be left at the -1 sentinel and not at a stale fd.
     * This pins nothing about C6 — the pre-fix exit path assigned -1 here as well, it just
     * did the close and the assignment as two separate steps. Section 9 is where the claim
     * itself is tested. */
    TEST_ASSERT_EQUAL_MESSAGE(-1, desc->listen_sock,
        "the acceptor's exit path must take the listen socket out of the descriptor");

    free(desc);
}

/* When the acceptor accepts a connection but xTaskCreate() for the receiver
 * task fails, active_connections must be decremented back to 0 (and the client
 * socket closed).  Otherwise the leaked count keeps tcp_server_deinit()'s
 * wait-for-zero loop spinning forever, hanging the single httpd worker.
 *
 * Regression for tcp_server.c:292-298 (the failed-spawn rollback).
 *
 * Scenario (acceptor task body, driven synchronously):
 *   iter 1:
 *     check_task_exit_req()  → WaitBits #1 → 0
 *     accept()               → fd 10
 *     malloc(args)           → OK
 *     active_connections     → 1   (__atomic_fetch_add)
 *     xTaskCreate(receiver)  → pdFAIL  (should_fail)
 *       → free(args), close(client), active_connections → 0  (rollback)
 *   iter 2:
 *     check_task_exit_req()  → WaitBits #2 → EVENT_TASK_EXIT_REQ → break
 *
 * Expected: active_connections == 0 after the acceptor returns.
 * With the rollback removed, the count would stay at 1 and deinit would hang. */
void test_acceptor_decrements_on_receiver_spawn_failure(void)
{
    /* accept() dispenses fd 10 once, then -1 on the next call. */
    mock_accept_fd = 10;

    /* EXIT_REQ fires on the 2nd xEventGroupWaitBits call (called > 1), i.e. the
     * top-of-loop check of the SECOND iteration, so the loop exits right after
     * the failed receiver spawn. */
    mock_xEventGroupWaitBits_data.set_event_on_call = 1;        /* trigger after 1st call */
    mock_xEventGroupWaitBits_data.events_to_set     = (1 << 8); /* EVENT_TASK_EXIT_REQ = BIT8 */

    /* Init must SUCCEED first (acceptor task spawn must pass) — only the LATER
     * receiver spawn inside the loop should fail.  So enable should_fail AFTER
     * init records the acceptor task function. */
    tcp_desc_t *desc = NULL;
    esp_err_t ret = tcp_server_init(502, stub_receive_handler, &desc);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_NULL(desc);
    TEST_ASSERT_EQUAL(0, desc->active_connections);
    TEST_ASSERT_NOT_NULL(mock_xTaskCreate_data.pvTaskCode);

    /* From now on every xTaskCreate() fails — this is the receiver-task spawn. */
    mock_xTaskCreate_data.should_fail = true;

    /* Invoke the acceptor task body synchronously.  Both the function pointer
     * and its parameter are read before the call, so the inner failing
     * xTaskCreate() that overwrites mock_xTaskCreate_data does not affect us. */
    mock_xTaskCreate_data.pvTaskCode(mock_xTaskCreate_data.pvParameters);

    /* The failed spawn must have rolled the count back to 0 so deinit can finish. */
    TEST_ASSERT_EQUAL(0, desc->active_connections);
    /* The accepted client socket must have been closed on the rollback path. */
    TEST_ASSERT_GREATER_OR_EQUAL(1, mock_close_call_count);
    /* The acceptor registers last_client_sock when it admits the client, so the same
     * rollback must also un-register it: this connection is never served and its socket
     * was just closed, so leaving it as the send target would point tcp_server_send()
     * at a dead fd. */
    TEST_ASSERT_EQUAL_MESSAGE(-1, desc->last_client_sock,
        "a failed receiver spawn must roll last_client_sock back to the -1 sentinel");

    free(desc);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Section 4: tcp_server_connected()
 * ═══════════════════════════════════════════════════════════════════════════ */

/* tcp_server_connected returns ESP_FAIL when active_connections == 0 */
void test_tcp_server_connected_no_connections(void)
{
    tcp_desc_t desc = {0};
    desc.active_connections = 0;

    TEST_ASSERT_EQUAL(ESP_FAIL, tcp_server_connected(&desc));
}

/* tcp_server_connected returns ESP_OK when active_connections > 0 */
void test_tcp_server_connected_with_connections(void)
{
    tcp_desc_t desc = {0};
    desc.active_connections = 2;

    TEST_ASSERT_EQUAL(ESP_OK, tcp_server_connected(&desc));
}

/* tcp_server_connected returns ESP_FAIL for NULL desc */
void test_tcp_server_connected_null_desc(void)
{
    TEST_ASSERT_EQUAL(ESP_FAIL, tcp_server_connected(NULL));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Section 5: tcp_server_send()
 * ═══════════════════════════════════════════════════════════════════════════ */

/* tcp_server_send returns ESP_OK and calls send() once when send succeeds.
 * Mutant inverts the error check (res < 0 → res > 0): a successful send
 * (res == len > 0) would be misread as an error and return ESP_FAIL. */
void test_tcp_server_send_success_returns_ok(void)
{
    tcp_desc_t desc = {0};
    desc.active_connections = 1;
    desc.port = 502;

    uint8_t payload[4] = { 0x01, 0x02, 0x03, 0x04 };
    esp_err_t ret = tcp_server_send(&desc, 10, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(1, mock_send_call_count);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Section 6: last_client_sock tracking
 * ═══════════════════════════════════════════════════════════════════════════ */

/* After init, last_client_sock must be the -1 "no client" sentinel.
 * Mutant initializes it to 0 (which calloc already provides), so a 0 value
 * would be indistinguishable from a real socket fd 0. */
void test_tcp_server_init_sets_no_client_sentinel(void)
{
    tcp_desc_t *desc = NULL;

    esp_err_t ret = tcp_server_init(502, stub_receive_handler, &desc);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_NULL(desc);
    TEST_ASSERT_EQUAL(-1, desc->last_client_sock);

    free(desc);
}

/* On data receipt the receiver must record the real client socket in
 * last_client_sock so consumers can reply to the last sender.  Mutant sets
 * it to -1 instead of the real sock.
 *
 * Sampled from inside the receive handler, which is the only point where the value is
 * meaningful: the teardown at the end of run_receiver() clears the field again (that is
 * what retires the connection), so checking it after the call would only prove the
 * teardown ran. */
static int g_last_client_sock_in_handler = -99;

static void sampling_receive_handler(tcp_desc_t *desc, int client_sock, uint8_t *data, size_t len)
{
    (void)data;
    g_receive_handler_called++;
    g_receive_handler_sock = client_sock;
    g_receive_handler_len = len;
    g_last_client_sock_in_handler = desc->last_client_sock;
}

void test_receiver_records_last_client_sock_on_data(void)
{
    tcp_desc_t desc = {0};
    desc.receive_handler = sampling_receive_handler;
    desc.close_handler = stub_close_handler;
    desc.active_connections = 1;
    desc.port = 502;
    desc.last_client_sock = -1;
    /* Non-NULL handle: run_receiver checks the exit flag after each packet via
     * check_task_exit_req(); the mock returns 0 (no exit) so the loop continues. */
    desc.event_group = (EventGroupHandle_t)0xDEADBEEF;

    g_last_client_sock_in_handler = -99;

    mock_recv_data[0] = 0xAB;
    mock_recv_data_len = 1;

    /* First recv returns 1 byte, second returns 0 (disconnect) */
    mock_recv_return_values[0] = 1;
    mock_recv_return_values[1] = 0;
    mock_recv_return_count = 2;

    tcp_server_run_receiver_for_test(&desc, 10);

    TEST_ASSERT_EQUAL(1, g_receive_handler_called);
    TEST_ASSERT_EQUAL_MESSAGE(10, g_last_client_sock_in_handler,
        "while the connection is live, last_client_sock must name its socket");
    TEST_ASSERT_EQUAL_MESSAGE(-1, desc.last_client_sock,
        "retiring the connection must clear the send target back to the -1 sentinel");
}

/* B3 regression: the receiver must register last_client_sock when the connection
 * is ADMITTED, before it ever recv()s — not on the first received packet.
 *
 * Bug: a transparent bridge in server mode could not push serial->TCP to a client
 * that had connected but not yet sent anything: tcp_server_connected() already
 * passed (active_connections != 0) while last_client_sock was still the -1
 * sentinel, so tcp_server_send() logged "No client connected" and dropped the data
 * until the client happened to speak first.
 *
 * The recv hook samples last_client_sock on entry to the FIRST recv() call, which is
 * strictly before any data can have been processed.  Pre-fix the sample is -1. */
static tcp_desc_t *g_hook_desc = NULL;
static int g_last_client_sock_at_first_recv = -99;

static void sample_last_client_sock_on_first_recv(int call_index)
{
    if ((call_index == 0) && g_hook_desc) {
        g_last_client_sock_at_first_recv = g_hook_desc->last_client_sock;
    }
}

void test_receiver_registers_last_client_sock_before_first_recv(void)
{
    tcp_desc_t desc = {0};
    desc.receive_handler = stub_receive_handler;
    desc.close_handler = NULL;
    desc.active_connections = 1;
    desc.port = 502;
    desc.last_client_sock = -1;   /* state left by tcp_server_init() */

    g_hook_desc = &desc;
    g_last_client_sock_at_first_recv = -99;
    mock_recv_hook = sample_last_client_sock_on_first_recv;

    /* The client never sends anything: the first recv() reports a clean disconnect. */
    mock_recv_return_values[0] = 0;
    mock_recv_return_count = 1;

    tcp_server_run_receiver_for_test(&desc, 10);

    mock_recv_hook = NULL;
    g_hook_desc = NULL;

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_recv_call_count, "recv() must have been entered once");
    TEST_ASSERT_EQUAL_MESSAGE(10, g_last_client_sock_at_first_recv,
        "admitted client must be registered in last_client_sock before the first recv()");
}

/* A silent client (connects, sends nothing, disconnects) must still have been
 * reachable for the whole life of its connection.  Sampled at close_handler time,
 * which run_receiver invokes after the recv loop but before it tears the socket down. */
static int g_last_client_sock_at_close = -99;

static void sampling_close_handler(tcp_desc_t *desc, int client_sock)
{
    (void)client_sock;
    g_close_handler_called++;
    g_last_client_sock_at_close = desc->last_client_sock;
}

void test_receiver_registration_survives_silent_client(void)
{
    tcp_desc_t desc = {0};
    desc.receive_handler = stub_receive_handler;
    desc.close_handler = sampling_close_handler;
    desc.active_connections = 1;
    desc.port = 502;
    desc.last_client_sock = -1;

    g_last_client_sock_at_close = -99;

    /* recv() returns 0 straight away — the client never sent a byte. */
    mock_recv_return_values[0] = 0;
    mock_recv_return_count = 1;

    tcp_server_run_receiver_for_test(&desc, 10);

    TEST_ASSERT_EQUAL_MESSAGE(0, g_receive_handler_called,
        "no data was received, so receive_handler must not fire");
    TEST_ASSERT_EQUAL_MESSAGE(10, g_last_client_sock_at_close,
        "a client that never sent data must still be registered as last_client_sock");
}

/* The acceptor's block-new cap must not disturb the client currently being served:
 * a rejected newcomer never reaches run_receiver(), so it cannot overwrite
 * last_client_sock.  This is why registration lives in run_receiver() and not in
 * the accept path.
 *
 * Scenario (acceptor task body, driven synchronously):
 *   iter 1: exit check → 0; accept() → fd 10; active_connections (1) >= max (1)
 *           → reject: close(10), continue — no malloc, no receiver spawn
 *   iter 2: exit check → EVENT_TASK_EXIT_REQ → break */
void test_acceptor_rejection_does_not_touch_last_client_sock(void)
{
    mock_accept_fd = 10;   /* the newcomer that must be rejected */

    /* EXIT_REQ fires on the 2nd xEventGroupWaitBits call (top of iteration 2). */
    mock_xEventGroupWaitBits_data.set_event_on_call = 1;
    mock_xEventGroupWaitBits_data.events_to_set     = (1 << 8);  /* EVENT_TASK_EXIT_REQ = BIT8 */

    tcp_desc_t *desc = NULL;
    esp_err_t ret = tcp_server_init(502, stub_receive_handler, &desc);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_NULL(desc);
    TEST_ASSERT_NOT_NULL(mock_xTaskCreate_data.pvTaskCode);

    /* One client (fd 7) is already admitted and being served, as transparent_tcp
     * configures it: a single connection at a time, newcomers rejected. */
    tcp_server_set_max_connections(desc, 1);
    desc->active_connections = 1;
    desc->last_client_sock = 7;

    mock_xTaskCreate_data.called = 0;
    mock_xTaskCreate_data.pvTaskCode(mock_xTaskCreate_data.pvParameters);

    TEST_ASSERT_EQUAL_MESSAGE(7, desc->last_client_sock,
        "a client rejected by the connection cap must not steal the served client's fd");
    TEST_ASSERT_EQUAL_MESSAGE(1, desc->active_connections,
        "a rejected client must not be counted as an active connection");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xTaskCreate_data.called,
        "no receiver task may be spawned for a rejected client");

    free(desc);
}

/* The acceptor itself must register the admitted socket, and must do so BEFORE it
 * publishes the connection via active_connections: tcp_server_connected() flips to
 * "connected" on that increment, so a consumer reading last_client_sock in the gap
 * before the receiver task gets scheduled would otherwise still see -1 and drop the
 * data ("No client connected") — the residual form of the B3 bug.
 *
 * The xTaskCreate mock only records pvTaskCode without running it (self_execution is
 * off by default), so after a SUCCESSFUL accept the receiver task has not run at all.
 * Whatever is observable here was therefore written by the acceptor alone, which is
 * exactly the state a consumer would see inside the window.
 *
 * What this canNOT show: the two writes are checked after the fact, so a
 * single-threaded mock cannot prove the store ORDER against a concurrent reader.
 * The ordering is enforced in the source (registration sits directly above the
 * __atomic_fetch_add) and by the SEQ_CST increment; this test pins that the acceptor
 * registers at all, which is the part that regressed. */
void test_acceptor_registers_last_client_sock_on_admit(void)
{
    mock_accept_fd = 10;

    /* EXIT_REQ fires on the 2nd xEventGroupWaitBits call (top of iteration 2), so the
     * acceptor performs exactly one successful admit and then leaves the loop. */
    mock_xEventGroupWaitBits_data.set_event_on_call = 1;
    mock_xEventGroupWaitBits_data.events_to_set     = (1 << 8);  /* EVENT_TASK_EXIT_REQ = BIT8 */

    tcp_desc_t *desc = NULL;
    esp_err_t ret = tcp_server_init(502, stub_receive_handler, &desc);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_NULL(desc);
    TEST_ASSERT_EQUAL_MESSAGE(-1, desc->last_client_sock,
        "init must leave the -1 sentinel before any client is admitted");

    /* Read the acceptor's entry point before invoking it: the receiver spawn inside
     * the loop overwrites mock_xTaskCreate_data. */
    TaskFunction_t acceptor = mock_xTaskCreate_data.pvTaskCode;
    void *acceptor_args = mock_xTaskCreate_data.pvParameters;
    TEST_ASSERT_NOT_NULL(acceptor);

    acceptor(acceptor_args);

    /* The receiver task never ran, so this is purely the acceptor's own state: the
     * connection is published AND already routable. */
    TEST_ASSERT_EQUAL_MESSAGE(1, desc->active_connections,
        "a successfully admitted client must be counted as an active connection");
    TEST_ASSERT_EQUAL_MESSAGE(10, desc->last_client_sock,
        "the acceptor must register the admitted socket, so the client is reachable "
        "before its receiver task is scheduled");
    /* Admitting a connection must NOT move the generation on. A pair captured before this
     * admit is still safe: the fd this client got was free only because the socket that held
     * it had been closed, and every close goes through retire_client_conn(), which bumps. An
     * extra bump here would only invalidate the in-flight replies of the OTHER clients
     * sharing this descriptor — visible on the uncapped modbus gateway, where a stranger
     * connecting would cost every master a retry. */
    TEST_ASSERT_EQUAL_MESSAGE(0u, tcp_desc_conn_generation(desc),
        "admitting a connection must not bump the connection generation");

    /* The receiver spawn SUCCEEDED, so the acceptor handed its heap-allocated args to a
     * task that normally frees them (receiver_task). self_execution is off, so that task
     * never ran: free the args here instead. mock_xTaskCreate_data still holds them —
     * the receiver spawn was the last xTaskCreate call. */
    free(mock_xTaskCreate_data.pvParameters);
    free(desc);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Section 7: connection lock and generation (C2 — serial→TCP send racing a close)
 *
 * The bug: a producer task (uart_event_task) read desc->last_client_sock, passed the fd
 * by value into the send path, and was preempted. The receiver task closed that socket,
 * lwIP handed the fd number to the next socket in the system, and the producer then
 * send()ed RS-485 bytes into a completely unrelated TCP connection.
 *
 * Two things are needed to close it, and both are exercised here: the LOCK, which makes
 * "resolve/validate the target and send" atomic against "clear the target and close", and
 * the GENERATION, because after fd reuse the number alone still matches.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* A live descriptor as the production code would leave it: one admitted client on
 * client_sock and a real connection lock. */
static void make_connected_desc(tcp_desc_t *desc, int client_sock)
{
    memset(desc, 0, sizeof(*desc));
    desc->receive_handler = stub_receive_handler;
    desc->active_connections = 1;
    desc->port = 502;
    desc->last_client_sock = client_sock;
    desc->conn_lock = MOCK_SEMAPHORE_HANDLE_T;
}

/* tcp_server_send_to_current_client() resolves the target itself, under the lock, and
 * waits for that lock only for a bounded time — the caller is the UART event task, which
 * must not be parked behind a teardown (that is the whole reason the send below is
 * MSG_DONTWAIT).  Mutant: portMAX_DELAY instead of the bounded wait. */
void test_send_to_current_client_resolves_target_under_bounded_lock(void)
{
    tcp_desc_t desc;
    make_connected_desc(&desc, 10);

    uint8_t payload[4] = { 0x01, 0x02, 0x03, 0x04 };
    esp_err_t ret = tcp_server_send_to_current_client(&desc, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_send_call_count, "the packet must reach send()");
    TEST_ASSERT_EQUAL_MESSAGE(10, mock_send_last_fd,
        "the registered client socket must be the send target");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xSemaphoreTake_called,
        "the target must be resolved with the connection lock held");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(portMAX_DELAY, mock_xSemaphoreTake_xTicksToWait,
        "a producer task must never block indefinitely on the connection lock");
    TEST_ASSERT_EQUAL_MESSAGE(pdMS_TO_TICKS(TCP_DESC_SEND_LOCK_TIMEOUT_MS),
        mock_xSemaphoreTake_xTicksToWait,
        "the producer-side wait must be TCP_DESC_SEND_LOCK_TIMEOUT_MS");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphore_held_count,
        "the connection lock must be released again");
}

/* If the lock cannot be taken within that bounded wait, the packet is dropped — it must
 * NOT be sent unsynchronised "just this once", because that is precisely the window in
 * which the socket is being closed. */
void test_send_to_current_client_drops_packet_when_lock_unavailable(void)
{
    tcp_desc_t desc;
    make_connected_desc(&desc, 10);

    mock_xSemaphoreTake_return_value = pdFAIL;   /* lock held by a teardown */

    uint8_t payload[4] = { 0x01, 0x02, 0x03, 0x04 };
    esp_err_t ret = tcp_server_send_to_current_client(&desc, payload, sizeof(payload));

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, ret, "a packet that could not be locked is dropped");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_send_call_count,
        "nothing may be sent while the connection state cannot be validated");
}

/* A capture that predates a connection change must be refused. */
void test_send_to_captured_client_rejects_stale_generation(void)
{
    tcp_desc_t desc;
    make_connected_desc(&desc, 10);
    desc.conn_generation = 5;

    uint8_t payload[4] = { 0x01, 0x02, 0x03, 0x04 };
    esp_err_t ret = tcp_server_send_to_captured_client(&desc, 10, 4, payload, sizeof(payload));

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, ret, "a stale capture must not be sent");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_send_call_count,
        "a response captured before a connection change must never reach send()");
}

/* Control for the test above: with a matching generation the same call goes through, so
 * the rejection is the generation check and not a blanket refusal. */
void test_send_to_captured_client_sends_when_generation_matches(void)
{
    tcp_desc_t desc;
    make_connected_desc(&desc, 10);
    desc.conn_generation = 5;

    uint8_t payload[4] = { 0x01, 0x02, 0x03, 0x04 };
    esp_err_t ret = tcp_server_send_to_captured_client(&desc, 10, 5, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(1, mock_send_call_count);
    TEST_ASSERT_EQUAL(10, mock_send_last_fd);
}

/* THE regression test for C2.
 *
 * Client A is admitted on fd 54 and a consumer captures (54, generation) — this is what
 * modbus_tcp stores next to pending_client_sock while its RTU request is out on RS-485.
 * A then disconnects and lwIP hands fd 54 straight back out to client B. The captured
 * response must NOT be delivered to B.
 *
 * Note what an fd comparison would do here: desc->last_client_sock is 54 again, so
 * "captured_sock == last_client_sock" passes and B receives A's RS-485 bytes. Only the
 * generation distinguishes the two connections. Mutant: replace the generation check with
 * that fd comparison and this test goes red while everything else stays green. */
void test_stale_capture_rejected_after_fd_reused_by_new_connection(void)
{
    /* ---- Client A is admitted on fd 54 by the real acceptor ---------------- */
    mock_accept_fd = 54;
    mock_xEventGroupWaitBits_data.set_event_on_call = 1;         /* exit on iteration 2 */
    mock_xEventGroupWaitBits_data.events_to_set     = (1 << 8);  /* EVENT_TASK_EXIT_REQ */

    tcp_desc_t *desc = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, tcp_server_init(502, stub_receive_handler, &desc));
    TEST_ASSERT_NOT_NULL(desc);

    TaskFunction_t acceptor = mock_xTaskCreate_data.pvTaskCode;
    void *acceptor_args = mock_xTaskCreate_data.pvParameters;
    TEST_ASSERT_NOT_NULL(acceptor);

    acceptor(acceptor_args);
    /* self_execution is off, so the receiver task never ran and never freed its args. */
    free(mock_xTaskCreate_data.pvParameters);

    TEST_ASSERT_EQUAL_MESSAGE(54, desc->last_client_sock, "client A must be registered");

    /* ---- A consumer captures the connection it is talking to --------------- */
    int      captured_sock = desc->last_client_sock;
    uint32_t captured_gen  = tcp_desc_conn_generation(desc);

    /* ---- A disconnects: its receiver runs to completion and retires the socket */
    mock_recv_return_values[0] = 0;      /* clean disconnect on the first recv() */
    mock_recv_return_count = 1;
    tcp_server_run_receiver_for_test(desc, 54);
    TEST_ASSERT_EQUAL_MESSAGE(-1, desc->last_client_sock,
        "retiring A must clear the send target");

    /* ---- Client B connects and lwIP recycles the very same fd number -------- */
    mock_freertos_event_groups_reset();                          /* clear the sticky exit bit */
    mock_xEventGroupWaitBits_data.set_event_on_call = 1;
    mock_xEventGroupWaitBits_data.events_to_set     = (1 << 8);
    mock_accept_fd      = 54;
    mock_send_call_count = 0;
    /* Re-arm the listen socket. Driving the acceptor a second time is a fiction of this
     * test — in production the task body runs once — and its exit path CLAIMED the listen
     * socket on the way out (see Section 9), leaving the -1 sentinel. The loop now refuses
     * to accept() on a claimed socket, so B's connection needs a live listener again; 5 is
     * the fd tcp_server_init() was given by the socket mock. */
    desc->listen_sock = 5;

    acceptor(acceptor_args);
    free(mock_xTaskCreate_data.pvParameters);

    TEST_ASSERT_EQUAL_MESSAGE(54, desc->last_client_sock,
        "the new client must hold the same fd number — that is the point of this test");

    /* ---- The stale capture must not reach B -------------------------------- */
    uint8_t payload[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    esp_err_t ret = tcp_server_send_to_captured_client(desc, captured_sock, captured_gen,
                                                       payload, sizeof(payload));

    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, ret,
        "a capture from the previous connection must be rejected");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_send_call_count,
        "RS-485 bytes addressed to client A must never land on client B's connection");

    /* Control: B's own (fd, generation) pair is accepted. */
    ret = tcp_server_send_to_captured_client(desc, 54, tcp_desc_conn_generation(desc),
                                             payload, sizeof(payload));
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "the current connection must still be reachable");
    TEST_ASSERT_EQUAL(1, mock_send_call_count);

    free(desc);
}

/* Retiring a connection must both clear the send target and move the generation on:
 * clearing alone would still let a pair captured earlier match after fd reuse. */
void test_receiver_teardown_bumps_conn_generation(void)
{
    tcp_desc_t desc;
    make_connected_desc(&desc, 10);
    desc.conn_generation = 7;

    mock_recv_return_values[0] = 0;
    mock_recv_return_count = 1;

    tcp_server_run_receiver_for_test(&desc, 10);

    TEST_ASSERT_EQUAL_MESSAGE(-1, desc.last_client_sock,
        "the retired socket must stop being the send target");
    TEST_ASSERT_EQUAL_MESSAGE(8, desc.conn_generation,
        "retiring a connection must invalidate every pair captured against it");
}

/* close() must run INSIDE the locked region, not merely after the field was cleared.
 * Clearing the field does not help a producer that already resolved the fd and is on its
 * way into send(): only holding the lock across close() keeps it out.
 *
 * Observed through the close hook: the semaphore mock counts outstanding takes, so a
 * non-zero count at close time means the lock is held. Mutant: move close() below
 * tcp_desc_conn_lock_release() and this goes red. */
static int g_lock_held_at_close = -1;

static void sample_lock_held_on_close(int fd)
{
    (void)fd;
    g_lock_held_at_close = mock_xSemaphore_held_count;
}

void test_receiver_closes_socket_while_holding_conn_lock(void)
{
    tcp_desc_t desc;
    make_connected_desc(&desc, 10);

    g_lock_held_at_close = -1;
    mock_close_hook = sample_lock_held_on_close;

    mock_recv_return_values[0] = 0;
    mock_recv_return_count = 1;

    tcp_server_run_receiver_for_test(&desc, 10);

    mock_close_hook = NULL;

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_close_call_count,
        "the client socket must be closed exactly once");
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, g_lock_held_at_close,
        "close() must run with the connection lock held, or a producer can still be "
        "inside send() with the fd being recycled");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphore_held_count,
        "the connection lock must be released once the socket is gone");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Section 8: the listening socket's address family and bind address (B8)
 *
 * The socket layer is mocked here, so these tests cannot exercise lwIP's collision
 * semantics — what they CAN pin down is the two inputs those semantics are decided from,
 * and both of them are one careless edit away from silently reverting.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Overwrite the stack region that tcp_server_init() and create_listen_socket() are about
 * to use with a recognisable pattern.
 *
 * Without this, "the address struct was never zeroed" would be invisible: an uninitialised
 * sockaddr_storage on a fresh stack page reads back as zeroes, and the test below would
 * pass on code that only works by luck. Poisoning first makes the missing memset() show up
 * as 0xEE in sin6_flowinfo / sin6_scope_id.
 *
 * 4 KB covers the two frames involved with room to spare; volatile keeps the write from
 * being elided as dead (the suite builds at -O0, but that is not a property to depend on). */
static void poison_stack_below(void)
{
    volatile uint8_t scratch[4096];
    memset((void *)scratch, 0xEE, sizeof(scratch));
}

/* Regression closed: create_listen_socket() going back to AF_INET.
 *
 * esp_http_server binds PF_INET6/in6addr_any, which lwIP turns into a dual-stack
 * IPADDR_TYPE_ANY pcb. While this socket asked for AF_INET, tcp_listen()'s duplicate check
 * compared the two with ip_addr_eq(), which is 0 whenever IP_GET_TYPE differs — so a bridge
 * or the cache Modbus server could listen on a port httpd already held, and the port then
 * answered HTTP and Modbus on alternate connections. Only the family requested here decides
 * that; nothing else in this file would change if it were wrong. */
void test_listen_socket_requests_dual_stack_family(void)
{
    tcp_desc_t *desc = NULL;

    esp_err_t ret = tcp_server_init(502, stub_receive_handler, &desc);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "init must succeed with the default mocks");
    TEST_ASSERT_EQUAL_MESSAGE(AF_INET6, mock_socket_last_domain,
        "the listen socket must be AF_INET6: an AF_INET one is invisible to httpd's "
        "dual-stack pcb and silently shares the port with it");
    TEST_ASSERT_EQUAL_MESSAGE(SOCK_STREAM, mock_socket_last_type,
        "still a stream socket");
    TEST_ASSERT_EQUAL_MESSAGE(IPPROTO_IP, mock_socket_last_protocol,
        "protocol 0 = the default for this family/type, the same value httpd passes; "
        "IPPROTO_IPV6 is an option level, not a protocol a SOCK_STREAM socket can carry");

    free(desc);
}

/* Regression closed: binding a partially filled sockaddr_in6.
 *
 * The address struct is a bare stack object and the IPv6 form has two fields the IPv4 one
 * did not. lwIP reads sin6_scope_id on the bind path and zones the address with it when the
 * address has a scope; a zoned address fails netconn_bind()'s ip_addr_eq(addr,
 * IP6_ADDR_ANY) test, dual stack is then NOT applied, and the result is a V6-only listener
 * no IPv4 client can reach — a far worse failure than the one being fixed, and one that no
 * other test in this suite would notice.
 *
 * So: the whole struct must arrive at bind() zeroed apart from the three fields that are
 * deliberately set. Asserting the two tail fields is the point of the test; family, address
 * and port are checked with it so a mutation cannot satisfy it by binding nothing useful. */
void test_listen_socket_binds_fully_zeroed_any_address(void)
{
    tcp_desc_t *desc = NULL;

    poison_stack_below();
    esp_err_t ret = tcp_server_init(8899, stub_receive_handler, &desc);

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "init must succeed with the default mocks");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_bind_call_count,
        "exactly one bind() — the retry loop must not have been entered");

    const struct sockaddr_in6 *bound = (const struct sockaddr_in6 *)&mock_bind_last_addr;

    TEST_ASSERT_EQUAL_MESSAGE(AF_INET6, bound->sin6_family,
        "the bound address must be the IPv6 form, matching the socket family");
    TEST_ASSERT_EQUAL_MESSAGE(htons(8899), bound->sin6_port,
        "the requested port must survive into the bind");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&in6addr_any, &bound->sin6_addr, sizeof(struct in6_addr),
        "must bind the unspecified address: that is the only value netconn_bind() promotes "
        "to the dual-stack IP_ANY_TYPE");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, bound->sin6_flowinfo,
        "sin6_flowinfo must be zeroed, not left as stack garbage");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, bound->sin6_scope_id,
        "sin6_scope_id must be zeroed: lwIP reads it, and a garbage zone costs the "
        "dual-stack promotion and with it every IPv4 client");
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE((int)sizeof(struct sockaddr_in6), (int)mock_bind_last_addrlen,
        "bind() must be told the address is at least the IPv6 form's length: lwIP validates "
        "namelen against sizeof(sockaddr_in)/sizeof(sockaddr_in6) and rejects anything else "
        "with EINVAL, which the retry loop would spend a second on before giving up");

    free(desc);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Section 9: listen-socket ownership (C6 — double close of the listen socket)
 *
 * The bug: desc->listen_sock had two owners with nothing serialising them — the acceptor
 * task and tcp_server_deinit(), which closes the socket as an opportunistic nudge for a
 * blocked accept() (the guarantee is the 200 ms SO_RCVTIMEO, not this close) — and "close
 * it" and "clear the field" were separate steps. deinit() closed the fd and left the number
 * in place, so the acceptor's exit path closed the same number a second time. (On the
 * recreate path the roles swap and the second close is the recreate branch's — the comment
 * on test_acceptor_skips_recreate_when_exit_requested_during_backoff spells that one out.)
 * That is not a harmless duplicate: nothing reserves the number between the two closes, so
 * any other task — httpd, another bridge, anything that opens an lwIP socket — can be handed
 * it back in the gap, and the second close then silently kills an unrelated live socket.
 *
 * The fix is close_listen_socket(): one atomic exchange takes the number OUT of the
 * descriptor and only the caller that came away with a non-negative fd closes it. These
 * tests observe that claim from inside the close hook, which is the only point where the
 * "was the field already cleared?" question has a meaningful answer.
 *
 * mock_socket_fd is a constant 5, so a recreate hands the SAME number back — the fd-reuse
 * hazard is modelled by construction here rather than having to be simulated.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Sampled from inside mock_close(), i.e. at the instant the fd is handed to close():
 * what the descriptor still says, and which fd is being closed. */
static tcp_desc_t *g_deinit_desc = NULL;
static int g_listen_sock_during_close = -99;
static int g_fd_at_close = -99;

static void sample_listen_sock_on_close(int fd)
{
    g_fd_at_close = fd;
    if (g_deinit_desc) {
        g_listen_sock_during_close = g_deinit_desc->listen_sock;
    }
}

/* The same sample, but kept per close, for the path that closes twice. */
#define MAX_TRACKED_CLOSES  4
static tcp_desc_t *g_close_watch_desc = NULL;
static int g_close_fd[MAX_TRACKED_CLOSES];
static int g_listen_sock_at_close[MAX_TRACKED_CLOSES];
static int g_close_records = 0;

static void record_close_against_desc(int fd)
{
    if (g_close_records < MAX_TRACKED_CLOSES) {
        g_close_fd[g_close_records] = fd;
        g_listen_sock_at_close[g_close_records] =
            g_close_watch_desc ? g_close_watch_desc->listen_sock : -99;
    }
    g_close_records++;
}

/* Every loop exit in tcp_server_task() ends at the same three lines: close_listen_socket(),
 * then xEventGroupSetBits(EVENT_TASK_FINISHED), then vTaskDelete(). An exit that skips the
 * middle one — a `return` where a `break` belongs — leaves tcp_server_deinit() parked on
 * that bit with portMAX_DELAY forever, taking the device's single httpd worker with it. So
 * every test of a new `break` asserts the bit, not just that something was signalled.
 *
 * EVENT_TASK_FINISHED is BIT1 in tcp_server.c; the acceptor sets no other bit anywhere, so
 * one call carrying exactly this mask is the whole expected signalling of a clean exit. */
#define EVENT_TASK_FINISHED_BIT     (1u << 1)

static void assert_signalled_task_finished_once(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xEventGroupSetBits_data.called,
        "the acceptor must signal exactly once on its way out");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(EVENT_TASK_FINISHED_BIT,
        mock_xEventGroupSetBits_data.uxBitsToSet[0],
        "and the bit must be EVENT_TASK_FINISHED — it is the one deinit() waits on forever");
}

/* tcp_server_deinit() must CLAIM the listen socket, not merely close it: by the time
 * close() runs, the descriptor must no longer name that fd.
 *
 * Pre-fix, deinit did `if (listen_sock >= 0) close(listen_sock);` and left the field
 * untouched, so this hook would see 5 — the live fd number that the acceptor task, waking
 * up on the exit flag, would then close a second time.
 *
 * Sampling has to happen inside the hook: deinit() free()s the descriptor before it
 * returns, so there is nothing left to read afterwards. */
void test_deinit_claims_listen_sock_before_closing_it(void)
{
    tcp_desc_t *desc = NULL;
    esp_err_t ret = tcp_server_init(502, stub_receive_handler, &desc);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_NULL(desc);
    TEST_ASSERT_EQUAL(5, desc->listen_sock);

    g_deinit_desc = desc;
    g_listen_sock_during_close = -99;
    g_fd_at_close = -99;
    mock_close_hook = sample_listen_sock_on_close;

    /* The acceptor task never runs here (no self_execution), so nothing would ever set
     * EVENT_TASK_FINISHED. deinit()'s join is bounded now and reports ESP_ERR_TIMEOUT when
     * the bit is missing, so the mock has to stand in for the acceptor's exit and report the
     * bit — otherwise this test would exercise the abandon-the-descriptor path instead of the
     * successful teardown it is about. active_connections is 0, so the second join passes on
     * its first look and deinit() runs straight through. */
    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_FINISHED_BIT;

    TEST_ASSERT_EQUAL(ESP_OK, tcp_server_deinit(desc));

    mock_close_hook = NULL;
    g_deinit_desc = NULL;

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_close_call_count,
        "deinit must close the listen socket exactly once");
    TEST_ASSERT_EQUAL_MESSAGE(5, g_fd_at_close,
        "the fd that was claimed is the one that must be closed");
    TEST_ASSERT_EQUAL_MESSAGE(-1, g_listen_sock_during_close,
        "the number must already be out of the descriptor when close() runs, or the "
        "acceptor's exit path closes the same fd again");
}

/* Both joins in tcp_server_deinit() are bounded, and an expired join must be REPORTED and
 * must leave the descriptor alone.
 *
 * The report matters because the only production callers run on the settings-apply path, which
 * the single esp_http_server worker joins: an unbounded join there is a device that answers no
 * HTTP request at all until it is power-cycled.
 *
 * Leaving the descriptor alone matters more. Everything deinit does after the joins —
 * vSemaphoreDelete() on conn_lock, vEventGroupDelete(), free() — is safe only because the joins
 * proved that no task is inside the descriptor any more. An expired join proves the opposite,
 * so tearing down anyway would hand a live acceptor a deleted event group (it reads it on every
 * loop, in check_task_exit_req()) and freed memory: a use-after-free instead of a hang, which is
 * strictly worse. vEventGroupDelete is the cheapest observable proxy for "the teardown ran".
 *
 * Here the mock never reports EVENT_TASK_FINISHED, i.e. the acceptor is still running. */
void test_deinit_reports_timeout_when_the_acceptor_never_finishes(void)
{
    tcp_desc_t *desc = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, tcp_server_init(502, stub_receive_handler, &desc));
    TEST_ASSERT_NOT_NULL(desc);

    mock_xEventGroupWaitBits_data.return_value = 0;   /* the acceptor never signals */

    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_TIMEOUT, tcp_server_deinit(desc),
        "an expired join must be reported to the caller, not swallowed as success");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_vEventGroupDelete_data.called,
        "the event group must outlive a failed deinit — the acceptor still reads it");

    free(desc);   /* deinit leaked it on purpose; the test owns it from here */
}

/* Same rule for the second join: the acceptor is out, but a receiver task has not reached its
 * active_connections decrement, so it is still inside run_receiver() — still logging desc->port
 * and still taking desc->conn_lock in retire_client_conn(). */
void test_deinit_reports_timeout_when_a_receiver_is_still_running(void)
{
    tcp_desc_t *desc = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, tcp_server_init(502, stub_receive_handler, &desc));
    TEST_ASSERT_NOT_NULL(desc);

    mock_xEventGroupWaitBits_data.return_value = EVENT_TASK_FINISHED_BIT;
    desc->active_connections = 1;                     /* one receiver never finishes */

    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_TIMEOUT, tcp_server_deinit(desc),
        "a receiver that never finished must fail the deinit, not stall it forever");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_vEventGroupDelete_data.called,
        "the descriptor must outlive a failed deinit — a receiver is still using it");

    free(desc);
}

/* The other half of the same claim: an acceptor that finds the field already at -1 —
 * because deinit() got there first — must not close anything.
 *
 * Pre-fix the exit path was an unconditional close(desc->listen_sock) followed by the
 * reset, so it would have issued close(-1) here; with the real deinit's stale value left
 * in place it would have issued a close of the live number instead. */
void test_acceptor_does_not_close_listen_sock_claimed_by_deinit(void)
{
    /* EXIT_REQ must be visible on the FIRST xEventGroupWaitBits call. set_event_on_call
     * only fires on calls strictly after it, so this case is expressed by seeding the
     * mock's return value directly. EVENT_TASK_EXIT_REQ = BIT8. */
    mock_xEventGroupWaitBits_data.return_value = (1 << 8);

    tcp_desc_t *desc = NULL;
    esp_err_t ret = tcp_server_init(502, stub_receive_handler, &desc);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_NULL(desc);
    TEST_ASSERT_NOT_NULL(mock_xTaskCreate_data.pvTaskCode);
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_close_call_count, "init must not have closed anything");

    /* Stands in for a tcp_server_deinit() that has already claimed the socket: the field
     * is at the sentinel and the fd it used to name has been closed and may already have
     * been handed to somebody else. */
    desc->listen_sock = -1;

    mock_xTaskCreate_data.pvTaskCode(mock_xTaskCreate_data.pvParameters);

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_close_call_count,
        "a socket somebody else has already claimed must not be closed again");

    free(desc);
}

/* The same guard one step earlier: the acceptor re-reads the field every iteration, and a
 * negative value means deinit() claimed the socket between iterations. It must leave the
 * loop instead of calling accept() on a closed (and possibly recycled) fd number.
 *
 * The exit flag is deliberately NOT set here, so the only thing that can stop the loop is
 * the field itself. Without the check the acceptor would accept() on -1, and the mock —
 * which does not care what fd it is given — would hand out a client and keep the loop
 * running until the event-group mock's call-tracking assert trips. */
void test_acceptor_stops_when_listen_sock_disappears_mid_loop(void)
{
    tcp_desc_t *desc = NULL;
    esp_err_t ret = tcp_server_init(502, stub_receive_handler, &desc);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_NULL(desc);
    TEST_ASSERT_NOT_NULL(mock_xTaskCreate_data.pvTaskCode);

    desc->listen_sock = -1;

    mock_xTaskCreate_data.pvTaskCode(mock_xTaskCreate_data.pvParameters);

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_accept_call_count,
        "there is nothing left to accept on once the listen socket has been claimed");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_close_call_count,
        "and nothing left to close either");
    /* The break lands on the ordinary exit code, so the task still reports itself finished —
     * deinit() waits on that bit and would otherwise never be released. */
    assert_signalled_task_finished_once();

    free(desc);
}

/* A deinit() that arrives during the recreate branch's 1000 ms backoff must not be
 * answered with a brand-new listener.
 *
 * accept() fails with an "unknown" errno (EINVAL — not EAGAIN, not the ENFILE class), so
 * the acceptor takes the branch that treats the listen socket as broken. It then sleeps a
 * second, which is ample time for a port-mode change to call deinit(): the socket has been
 * claimed and the port is being torn down.
 *
 * Pre-fix there was no check after the sleep, and the production sequence ran:
 *   deinit() close(5) and leaves 5 in the field   ← its close, with no claim
 *   the recreate branch close(desc->listen_sock) = close(5) a SECOND time, landing on
 *      whatever unrelated socket lwIP has handed fd 5 in the meantime
 *   create_listen_socket() may then be given 5 straight back
 *   the exit path closes that fresh fd — once, correctly
 * So the double close was the RECREATE BRANCH's, not the exit path's, and the fresh listener
 * that followed it was pure waste: up to another second of create_listen_socket() bind
 * retries with deinit blocked on EVENT_TASK_FINISHED for every millisecond of it.
 *
 * Drive sequence (the exit flag appears on WaitBits call #3):
 *   #1 top of the loop                          → 0
 *   accept() #1                                 → EINVAL
 *   #2 the check right after accept() failed    → 0
 *   vTaskDelay(1000 ms)
 *   #3 the post-backoff check                   → EXIT_REQ → break
 *   close_listen_socket()                        ← the one and only close
 *
 * Pre-fix the same sequence gives bind #2 (a fresh listener) and close #2. */
void test_acceptor_skips_recreate_when_exit_requested_during_backoff(void)
{
    mock_accept_fail_count = 1;
    mock_accept_errno      = EINVAL;   /* "unknown" errno → the close-and-recreate branch */

    mock_xEventGroupWaitBits_data.set_event_on_call = 2;        /* fires on call #3 */
    mock_xEventGroupWaitBits_data.events_to_set     = (1 << 8); /* EVENT_TASK_EXIT_REQ */

    tcp_desc_t *desc = NULL;
    esp_err_t ret = tcp_server_init(502, stub_receive_handler, &desc);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_NULL(desc);
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_bind_call_count, "init built the first listener");
    TEST_ASSERT_NOT_NULL(mock_xTaskCreate_data.pvTaskCode);

    mock_xTaskCreate_data.pvTaskCode(mock_xTaskCreate_data.pvParameters);

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_accept_call_count,
        "the loop must not come round for a second accept()");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_bind_call_count,
        "no listener may be built for a port that is being torn down");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_close_call_count,
        "only the exit path's claim-close, on the socket that was already there");
    TEST_ASSERT_EQUAL(-1, desc->listen_sock);
    /* The new break must land on the ordinary exit code, not walk out of the function: it is
     * the same three closing lines that release deinit(). */
    assert_signalled_task_finished_once();

    free(desc);
}

/* Control for the test above: with no exit request in the way, the recreate branch still
 * does its job — the broken listener is thrown away, a fresh one takes its place, and each
 * of the two sockets is closed exactly once.
 *
 * Same drive sequence, except the exit flag appears one call later (#4), i.e. at the top of
 * the second iteration rather than in the post-backoff check:
 *   #1 top of the loop                          → 0
 *   accept() #1                                 → EINVAL
 *   #2 the check right after accept() failed    → 0
 *   vTaskDelay(1000 ms)
 *   #3 the post-backoff check                   → 0
 *   close_listen_socket()                        ← close #1 (the broken listener)
 *   create_listen_socket()                       ← bind #2, same fd number back (fd 5)
 *   #4 top of the loop, 2nd iteration           → EXIT_REQ → break
 *   close_listen_socket()                        ← close #2 (the fresh listener)
 *
 * The fd number is the same both times, which is the whole point — and the reason the CLOSE
 * COUNT cannot carry this test. "Two sockets closed once each" and "one socket closed twice"
 * both come to 2. What separates them is sampled inside the close hook: each close must find
 * the descriptor ALREADY at -1, i.e. each was preceded by its own claim. Pre-fix, both
 * closers read the fd out of the field and left it there, so the hook would have caught 5
 * still sitting in the descriptor at close time — the number a second closer goes on to
 * close. */
void test_acceptor_recreates_listen_socket_and_closes_each_once(void)
{
    mock_accept_fail_count = 1;
    mock_accept_errno      = EINVAL;   /* "unknown" errno → the close-and-recreate branch */

    mock_xEventGroupWaitBits_data.set_event_on_call = 3;        /* fires on call #4 */
    mock_xEventGroupWaitBits_data.events_to_set     = (1 << 8); /* EVENT_TASK_EXIT_REQ */

    tcp_desc_t *desc = NULL;
    esp_err_t ret = tcp_server_init(502, stub_receive_handler, &desc);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_NULL(desc);
    TEST_ASSERT_EQUAL(1, mock_bind_call_count);
    TEST_ASSERT_NOT_NULL(mock_xTaskCreate_data.pvTaskCode);

    g_close_watch_desc = desc;
    g_close_records    = 0;
    mock_close_hook    = record_close_against_desc;

    mock_xTaskCreate_data.pvTaskCode(mock_xTaskCreate_data.pvParameters);

    mock_close_hook    = NULL;
    g_close_watch_desc = NULL;

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_accept_call_count,
        "the fresh listener is never accept()ed on: the exit flag is seen first");
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_bind_call_count,
        "the broken listener must be replaced by a fresh one");
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_close_call_count,
        "the recreate branch closes the broken listener, the exit path closes the fresh one");
    /* The property the count cannot see: BOTH closes were claim-closes. */
    TEST_ASSERT_EQUAL_MESSAGE(2, g_close_records,
        "the hook must have observed both closes");
    TEST_ASSERT_EQUAL_MESSAGE(5, g_close_fd[0],
        "close #1 is the broken listener");
    TEST_ASSERT_EQUAL_MESSAGE(-1, g_listen_sock_at_close[0],
        "the recreate branch must claim the broken listener before closing it");
    TEST_ASSERT_EQUAL_MESSAGE(5, g_close_fd[1],
        "close #2 is the fresh listener, handed the same fd number back");
    TEST_ASSERT_EQUAL_MESSAGE(-1, g_listen_sock_at_close[1],
        "the exit path must claim the fresh listener before closing it — a close that finds "
        "its own fd still in the descriptor is a close somebody else can repeat");
    TEST_ASSERT_EQUAL_MESSAGE(-1, desc->listen_sock,
        "the acceptor must hand the fresh socket over to close_listen_socket() too");
    TEST_ASSERT_EQUAL_MESSAGE(0, desc->active_connections,
        "no client was ever admitted on this path");

    free(desc);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Unity runner
 * ═══════════════════════════════════════════════════════════════════════════ */

int tcp_server_test(void)
{
    UNITY_BEGIN();

    /* Section 1 — receiver logic */
    RUN_TEST(test_receiver_calls_close_handler_on_disconnect);
    RUN_TEST(test_receiver_no_close_handler_when_null);
    RUN_TEST(test_receiver_calls_receive_handler_with_data);
    RUN_TEST(test_receiver_decrements_active_connections);
    RUN_TEST(test_receiver_sequence_data_then_disconnect);

    /* Section 2 — tcp_server_init */
    RUN_TEST(test_tcp_server_init_success);
    RUN_TEST(test_tcp_server_init_socket_fail);
    RUN_TEST(test_tcp_server_init_bind_fail);
    RUN_TEST(test_tcp_server_init_event_group_fail);
    RUN_TEST(test_tcp_server_init_task_create_fail);

    /* Section 3 — accept() ENFILE error handling */
    RUN_TEST(test_acceptor_enfile_does_not_close_listen_socket);
    RUN_TEST(test_acceptor_decrements_on_receiver_spawn_failure);

    /* Section 4 — active_connections */
    RUN_TEST(test_tcp_server_connected_no_connections);
    RUN_TEST(test_tcp_server_connected_with_connections);
    RUN_TEST(test_tcp_server_connected_null_desc);

    /* Section 5 — tcp_server_send */
    RUN_TEST(test_tcp_server_send_success_returns_ok);

    /* Section 6 — last_client_sock tracking */
    RUN_TEST(test_tcp_server_init_sets_no_client_sentinel);
    RUN_TEST(test_receiver_records_last_client_sock_on_data);
    RUN_TEST(test_receiver_registers_last_client_sock_before_first_recv);
    RUN_TEST(test_receiver_registration_survives_silent_client);
    RUN_TEST(test_acceptor_rejection_does_not_touch_last_client_sock);
    RUN_TEST(test_acceptor_registers_last_client_sock_on_admit);

    /* Section 7 — connection lock and generation (C2) */
    RUN_TEST(test_send_to_current_client_resolves_target_under_bounded_lock);
    RUN_TEST(test_send_to_current_client_drops_packet_when_lock_unavailable);
    RUN_TEST(test_send_to_captured_client_rejects_stale_generation);
    RUN_TEST(test_send_to_captured_client_sends_when_generation_matches);
    RUN_TEST(test_stale_capture_rejected_after_fd_reused_by_new_connection);
    RUN_TEST(test_receiver_teardown_bumps_conn_generation);
    RUN_TEST(test_receiver_closes_socket_while_holding_conn_lock);

    /* Section 8 — listen socket family and bind address (B8) */
    RUN_TEST(test_listen_socket_requests_dual_stack_family);
    RUN_TEST(test_listen_socket_binds_fully_zeroed_any_address);

    /* Section 9 — listen-socket ownership (C6) and bounded teardown joins */
    RUN_TEST(test_deinit_claims_listen_sock_before_closing_it);
    RUN_TEST(test_deinit_reports_timeout_when_the_acceptor_never_finishes);
    RUN_TEST(test_deinit_reports_timeout_when_a_receiver_is_still_running);
    RUN_TEST(test_acceptor_does_not_close_listen_sock_claimed_by_deinit);
    RUN_TEST(test_acceptor_stops_when_listen_sock_disappears_mid_loop);
    RUN_TEST(test_acceptor_skips_recreate_when_exit_requested_during_backoff);
    RUN_TEST(test_acceptor_recreates_listen_socket_and_closes_each_once);

    return UNITY_END();
}

int main(void)
{
    return tcp_server_test();
}
