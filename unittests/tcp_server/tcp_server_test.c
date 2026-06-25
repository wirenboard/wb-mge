#include "unity.h"

#include "tcp_server.h"
#include "tcp_desc.h"
#include "lwip/sockets.h"
#include "freertos/event_groups.h"
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
extern int  mock_accept_fd;
extern int  mock_accept_call_count;
extern int  mock_accept_fail_count;
extern int  mock_accept_errno;
extern int  mock_close_call_count;
extern int  mock_shutdown_call_count;
extern int  mock_send_call_count;
void mock_lwip_sockets_reset(void);

/* ── Mock state from freertos mocks ────────────────────────────────────── */
extern mock_xEventGroupCreate_t mock_xEventGroupCreate_data;
extern mock_xEventGroupWaitBits_t mock_xEventGroupWaitBits_data;
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
 * it to -1 instead of the real sock. */
void test_receiver_records_last_client_sock_on_data(void)
{
    tcp_desc_t desc = {0};
    desc.receive_handler = stub_receive_handler;
    desc.close_handler = stub_close_handler;
    desc.active_connections = 1;
    desc.port = 502;
    desc.last_client_sock = -1;

    mock_recv_data[0] = 0xAB;
    mock_recv_data_len = 1;

    /* First recv returns 1 byte, second returns 0 (disconnect) */
    mock_recv_return_values[0] = 1;
    mock_recv_return_values[1] = 0;
    mock_recv_return_count = 2;

    tcp_server_run_receiver_for_test(&desc, 10);

    TEST_ASSERT_EQUAL(1, g_receive_handler_called);
    TEST_ASSERT_EQUAL(10, desc.last_client_sock);
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

    return UNITY_END();
}

int main(void)
{
    return tcp_server_test();
}
