/*
 * sniffer_ws_dispatch_test.c — Unit tests for sniffer_ws_dispatch()
 *
 * TC-WS-1  No WS client connected → dispatch does nothing
 * TC-WS-2  WS client connected, fd valid → send_data called once with correct fd
 * TC-WS-3  Stale fd recycled as HTTP connection → send NOT called, fd cleared (bug 09)
 * TC-WS-4  Stale fd returns INVALID → send NOT called, fd cleared
 * TC-WS-5  send_data failure → fd cleared
 */

#include "unity.h"
#include "console_log.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* sniff_packet_t, sniffer_ws_handler, sniffer_ws_dispatch, sniffer_test_get_ws_client_fd
 * are all declared in sniffer.h under __unittest_env__. */
#include "sniffer.h"
#include "serial_mock.h"

/* FreeRTOS queue and timer mocks */
#include "freertos/queue.h"
#include "freertos/timers.h"

/* esp_http_server mock controls */
#include "esp_http_server.h"

/* ============================================================
 * Test fixtures
 * ============================================================ */

static serial_desc_t s_desc0;

/* ============================================================
 * setUp / tearDown
 * ============================================================ */

void setUp(void)
{
    mock_freertos_queue_reset();
    mock_freertos_timers_reset();
    mock_serial_reset();
    mock_esp_http_server_reset();

    memset(&s_desc0, 0, sizeof(s_desc0));

    sniffer_init();
    sniffer_attach(0, &s_desc0);
    sniffer_enable(0);
}

void tearDown(void) {}

/* ============================================================
 * Helper: build a minimal valid sniffer packet
 * ============================================================ */

static sniff_packet_t make_packet(void)
{
    sniff_packet_t pkt = {0};
    pkt.port      = 0;
    pkt.is_master = true;
    pkt.crc_valid = true;
    pkt.slave_id  = 1;
    pkt.function  = 3;
    pkt.data_len  = 8;
    return pkt;
}

/* ============================================================
 * TC-WS-1a: no WS client (fd == -1) → dispatch does nothing
 * ============================================================ */

void test_ws_dispatch_no_client(void)
{
    /* ws_client_fd starts at -1 after init — no client has called sniffer_ws_handler */
    sniff_packet_t pkt = make_packet();
    sniffer_ws_dispatch(&pkt);

    /* fd == -1: dispatch returns at the early-exit guard, touching no httpd functions */
    TEST_ASSERT_EQUAL(0, mock_httpd_ws_get_fd_info_called);
    TEST_ASSERT_EQUAL(0, mock_httpd_ws_send_data_called);
}

/* ============================================================
 * TC-WS-1b: fd valid but ws_server == NULL → dispatch does nothing
 * The early-exit guard checks both: fd == -1 || srv == NULL.
 * ============================================================ */

void test_ws_dispatch_no_server(void)
{
    /* Register a WS connection via sniffer_ws_handler so ws_client_fd=42 is set,
     * then immediately register again with handle=NULL to simulate ws_server cleared. */
    httpd_req_t ws_req = {
        .handle = NULL,
        .method = HTTP_GET,
        .aux    = (void *)(intptr_t)42,
    };
    sniffer_ws_handler(&ws_req);

    /* ws_client_fd=42 but ws_server=NULL — must not reach httpd */
    sniff_packet_t pkt = make_packet();
    sniffer_ws_dispatch(&pkt);

    TEST_ASSERT_EQUAL(0, mock_httpd_ws_get_fd_info_called);
    TEST_ASSERT_EQUAL(0, mock_httpd_ws_send_data_called);
}

/* ============================================================
 * TC-WS-2: valid WS client → send_data called with the correct fd
 * ============================================================ */

void test_ws_dispatch_valid_client_sends(void)
{
    /* Register a WS client: fd=42 via sniffer_ws_handler (HTTP_GET upgrade path) */
    httpd_req_t ws_req = {
        .handle = (httpd_handle_t)0xCAFE,
        .method = HTTP_GET,
        .aux    = (void *)(intptr_t)42,
    };
    sniffer_ws_handler(&ws_req);

    /* Default mock: get_fd_info returns HTTPD_WS_CLIENT_WEBSOCKET, send returns ESP_OK */
    sniff_packet_t pkt = make_packet();
    sniffer_ws_dispatch(&pkt);

    TEST_ASSERT_EQUAL(1, mock_httpd_ws_get_fd_info_called);
    TEST_ASSERT_EQUAL(42, mock_httpd_ws_get_fd_info_last_fd);
    TEST_ASSERT_EQUAL(1, mock_httpd_ws_send_data_called);
    TEST_ASSERT_EQUAL(42, mock_httpd_ws_send_data_last_fd);
}

/* ============================================================
 * TC-WS-3: stale fd reused as HTTP connection → no send, fd cleared (bug 09 fix)
 * ============================================================ */

void test_ws_dispatch_stale_fd_http_client_no_send(void)
{
    httpd_req_t ws_req = {
        .handle = (httpd_handle_t)0xCAFE,
        .method = HTTP_GET,
        .aux    = (void *)(intptr_t)42,
    };
    sniffer_ws_handler(&ws_req);

    /* Simulate fd recycled: OS gave the fd to a plain HTTP connection */
    mock_httpd_ws_get_fd_info_return = HTTPD_WS_CLIENT_HTTP;

    sniff_packet_t pkt = make_packet();
    sniffer_ws_dispatch(&pkt);

    TEST_ASSERT_EQUAL(1, mock_httpd_ws_get_fd_info_called);
    TEST_ASSERT_EQUAL(0, mock_httpd_ws_send_data_called);   /* must NOT send WS frame */
    TEST_ASSERT_EQUAL(-1, sniffer_test_get_ws_client_fd()); /* stale fd must be cleared */
}

/* ============================================================
 * TC-WS-4: stale fd returns INVALID → same protection applies
 * ============================================================ */

void test_ws_dispatch_stale_fd_invalid_no_send(void)
{
    httpd_req_t ws_req = {
        .handle = (httpd_handle_t)0xCAFE,
        .method = HTTP_GET,
        .aux    = (void *)(intptr_t)99,
    };
    sniffer_ws_handler(&ws_req);

    mock_httpd_ws_get_fd_info_return = HTTPD_WS_CLIENT_INVALID;

    sniff_packet_t pkt = make_packet();
    pkt.slave_id = 2;
    sniffer_ws_dispatch(&pkt);

    TEST_ASSERT_EQUAL(1, mock_httpd_ws_get_fd_info_called);
    TEST_ASSERT_EQUAL(0, mock_httpd_ws_send_data_called);
    TEST_ASSERT_EQUAL(-1, sniffer_test_get_ws_client_fd());
}

/* ============================================================
 * TC-WS-5: send_data failure → fd cleared
 * ============================================================ */

void test_ws_dispatch_send_failure_clears_client(void)
{
    httpd_req_t ws_req = {
        .handle = (httpd_handle_t)0xCAFE,
        .method = HTTP_GET,
        .aux    = (void *)(intptr_t)42,
    };
    sniffer_ws_handler(&ws_req);

    /* fd info says WS OK, but the actual send fails */
    mock_httpd_ws_get_fd_info_return = HTTPD_WS_CLIENT_WEBSOCKET;
    mock_httpd_ws_send_data_return   = ESP_FAIL;

    sniff_packet_t pkt = make_packet();
    sniffer_ws_dispatch(&pkt);

    TEST_ASSERT_EQUAL(1, mock_httpd_ws_send_data_called);
    TEST_ASSERT_EQUAL(-1, sniffer_test_get_ws_client_fd()); /* must clear after send failure */
}

/* NOTE: TC-WS-6 (race: ws_client_fd changed between dispatch snapshot and clear) cannot
 * be tested synchronously without an OS-level hook. Correctness is guaranteed by the
 * conditional clear "if (ws_client_fd == fd)" guard verified by code inspection. */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_ws_dispatch_no_client);
    RUN_TEST(test_ws_dispatch_no_server);
    RUN_TEST(test_ws_dispatch_valid_client_sends);
    RUN_TEST(test_ws_dispatch_stale_fd_http_client_no_send);
    RUN_TEST(test_ws_dispatch_stale_fd_invalid_no_send);
    RUN_TEST(test_ws_dispatch_send_failure_clears_client);

    return UNITY_END();
}
