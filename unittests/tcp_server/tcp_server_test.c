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
extern int  mock_close_call_count;
extern int  mock_shutdown_call_count;
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
 * Section 3: tcp_server_connected()
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

    /* Section 3 — active_connections */
    RUN_TEST(test_tcp_server_connected_no_connections);
    RUN_TEST(test_tcp_server_connected_with_connections);
    RUN_TEST(test_tcp_server_connected_null_desc);

    return UNITY_END();
}

int main(void)
{
    return tcp_server_test();
}
