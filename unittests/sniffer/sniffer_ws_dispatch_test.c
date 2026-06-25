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

/* cache_multimaster recording mock controls */
extern bool     mock_cache_multimaster_enabled;
extern void     mock_cache_multimaster_reset(void);
extern int      mock_cache_multimaster_on_request_called;
extern uint8_t  mock_cache_multimaster_on_request_last_port;
extern uint8_t  mock_cache_multimaster_on_request_last_slave_id;
extern uint8_t  mock_cache_multimaster_on_request_last_function;
extern uint16_t mock_cache_multimaster_on_request_last_start_reg;
extern uint16_t mock_cache_multimaster_on_request_last_count;
extern int      mock_cache_multimaster_on_response_called;
extern uint8_t  mock_cache_multimaster_on_response_last_port;
extern uint8_t  mock_cache_multimaster_on_response_last_slave_id;
extern uint8_t  mock_cache_multimaster_on_response_last_function;
extern uint16_t mock_cache_multimaster_on_response_last_data_len;
extern uint64_t mock_cache_multimaster_on_response_last_timestamp_us;
extern uint8_t  mock_cache_multimaster_on_response_last_data[];
extern bool     mock_cache_multimaster_on_response_last_data_null;
extern int      mock_cache_multimaster_clear_pending_called;
extern uint8_t  mock_cache_multimaster_clear_pending_last_port;

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
    mock_cache_multimaster_reset();

    memset(&s_desc0, 0, sizeof(s_desc0));

    sniffer_init();
    sniffer_attach(0, &s_desc0);
    sniffer_enable(0, SNIFF_REASON_DISPLAY);
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

/* ============================================================
 * TC-WS-7: cache enabled, 8-byte FC03 request → on_request fires (min len boundary)
 * ============================================================ */

void test_ws_dispatch_cache_request_min_len_8(void)
{
    mock_cache_multimaster_enabled = true;
    /* The cache is fed only for ports whose CACHE reason is set. */
    sniffer_enable(0, SNIFF_REASON_CACHE);

    sniff_packet_t pkt = make_packet();
    pkt.port = 0; pkt.slave_id = 1; pkt.function = 0x03;
    pkt.is_master = true; pkt.is_timeout = false; pkt.crc_valid = true;
    pkt.data_len = 8;
    pkt.data[0] = 1; pkt.data[1] = 0x03;
    pkt.data[2] = 0x00; pkt.data[3] = 0x64;  /* start_reg = 100 */
    pkt.data[4] = 0x00; pkt.data[5] = 0x0A;  /* count     = 10  */
    pkt.data[6] = 0x00; pkt.data[7] = 0x00;

    sniffer_ws_dispatch(&pkt);

    TEST_ASSERT_EQUAL(1, mock_cache_multimaster_on_request_called);
    TEST_ASSERT_EQUAL(0, mock_cache_multimaster_on_request_last_port);
    TEST_ASSERT_EQUAL(1, mock_cache_multimaster_on_request_last_slave_id);
    TEST_ASSERT_EQUAL(0x03, mock_cache_multimaster_on_request_last_function);
    TEST_ASSERT_EQUAL(100, mock_cache_multimaster_on_request_last_start_reg);
    TEST_ASSERT_EQUAL(10, mock_cache_multimaster_on_request_last_count);
}

/* ============================================================
 * TC-WS-8: cache enabled globally but CACHE reason NOT set on the port → no feed
 * ============================================================ */

void test_ws_dispatch_cache_not_fed_without_reason(void)
{
    mock_cache_multimaster_enabled = true;
    /* setUp only set DISPLAY on port 0; CACHE reason is absent. */

    sniff_packet_t pkt = make_packet();
    pkt.port = 0; pkt.slave_id = 1; pkt.function = 0x03;
    pkt.is_master = true; pkt.is_timeout = false; pkt.crc_valid = true;
    pkt.data_len = 8;
    pkt.data[0] = 1; pkt.data[1] = 0x03;
    pkt.data[2] = 0x00; pkt.data[3] = 0x64;
    pkt.data[4] = 0x00; pkt.data[5] = 0x0A;

    sniffer_ws_dispatch(&pkt);

    TEST_ASSERT_EQUAL(0, mock_cache_multimaster_on_request_called);
}

/* ============================================================
 * TC-WS-9: exception reply clears the pending request (corr-7)
 * ============================================================ */

void test_ws_dispatch_exception_clears_pending(void)
{
    mock_cache_multimaster_enabled = true;
    sniffer_enable(0, SNIFF_REASON_CACHE);

    /* Slave exception reply: function 0x83 (0x80 | FC03), not a cacheable FC. */
    sniff_packet_t pkt = make_packet();
    pkt.port = 0; pkt.slave_id = 1; pkt.function = 0x83;
    pkt.is_master = false; pkt.is_timeout = false; pkt.crc_valid = true;
    pkt.data_len = 5;
    pkt.data[0] = 1; pkt.data[1] = 0x83; pkt.data[2] = 0x02;

    sniffer_ws_dispatch(&pkt);

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_cache_multimaster_clear_pending_called,
        "an exception reply must clear the pending request (corr-7)");
    TEST_ASSERT_EQUAL(0, mock_cache_multimaster_clear_pending_last_port);
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_cache_multimaster_on_response_called,
        "an exception reply must NOT be treated as a cacheable response");
}

/* ============================================================
 * TC-WS-10: a bus timeout clears the pending request (corr-7)
 * ============================================================ */

void test_ws_dispatch_timeout_clears_pending(void)
{
    mock_cache_multimaster_enabled = true;
    sniffer_enable(0, SNIFF_REASON_CACHE);

    sniff_packet_t pkt = make_packet();
    pkt.port = 0; pkt.slave_id = 1; pkt.function = 0x03;
    pkt.is_master = false; pkt.is_timeout = true; pkt.crc_valid = false;
    pkt.data_len = 0;

    sniffer_ws_dispatch(&pkt);

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_cache_multimaster_clear_pending_called,
        "a bus timeout must clear the pending request (corr-7)");
    TEST_ASSERT_EQUAL(0, mock_cache_multimaster_on_response_called);
}

/* ============================================================
 * TC-WS-11: a valid response stores data and does NOT clear pending
 * ============================================================ */

void test_ws_dispatch_valid_response_no_clear(void)
{
    mock_cache_multimaster_enabled = true;
    sniffer_enable(0, SNIFF_REASON_CACHE);

    sniff_packet_t pkt = make_packet();
    pkt.port = 0; pkt.slave_id = 1; pkt.function = 0x03;
    pkt.is_master = false; pkt.is_timeout = false; pkt.crc_valid = true;
    pkt.data_len = 7;
    pkt.data[0] = 1; pkt.data[1] = 0x03; pkt.data[2] = 0x04;
    pkt.data[3] = 0xAA; pkt.data[4] = 0xBB; pkt.data[5] = 0xCC; pkt.data[6] = 0xDD;

    sniffer_ws_dispatch(&pkt);

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_cache_multimaster_on_response_called,
        "a valid response must be fed to on_response");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_cache_multimaster_clear_pending_called,
        "a valid response must NOT clear the pending request");
}

/* ============================================================
 * TC-WS-12: a master write-request (FC16) clears pending too (corr-7)
 * ============================================================ */

void test_ws_dispatch_master_write_clears_pending(void)
{
    mock_cache_multimaster_enabled = true;
    sniffer_enable(0, SNIFF_REASON_CACHE);

    /* Master FC16 (write multiple registers): is_master, valid CRC, but not a
     * cacheable read FC. It must clear any pending read, not leave it stale. */
    sniff_packet_t pkt = make_packet();
    pkt.port = 0; pkt.slave_id = 1; pkt.function = 0x10;  /* FC16 */
    pkt.is_master = true; pkt.is_timeout = false; pkt.crc_valid = true;
    pkt.data_len = 9;
    pkt.data[0] = 1; pkt.data[1] = 0x10;

    sniffer_ws_dispatch(&pkt);

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_cache_multimaster_clear_pending_called,
        "a non-read master request (FC16 write) must clear the pending read (corr-7)");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_cache_multimaster_on_request_called,
        "FC16 is not a cacheable read request");
}

/* ============================================================
 * Helpers for the timeout-suppression / cache-response group
 * ============================================================ */

/* Build a MASTER read request packet for the given FC, with the 8 PDU bytes the
 * request path needs (slave, fc, start_reg hi/lo, count hi/lo, +2 padding to data_len 8). */
static sniff_packet_t make_master_read_request(uint8_t fc, bool is_timeout)
{
    sniff_packet_t pkt = {0};
    pkt.port      = 0;
    pkt.slave_id  = 7;
    pkt.function  = fc;
    pkt.is_master = true;
    pkt.is_timeout = is_timeout;
    pkt.crc_valid = true;
    pkt.data_len  = 8;
    pkt.data[0] = 7;  pkt.data[1] = fc;
    pkt.data[2] = 0x00; pkt.data[3] = 0x10;  /* start_reg = 16 */
    pkt.data[4] = 0x00; pkt.data[5] = 0x05;  /* count     = 5  */
    pkt.data[6] = 0x00; pkt.data[7] = 0x00;
    return pkt;
}

/* ============================================================
 * TC-WS-13: a MASTER read request (FC01/02/03/04) with is_timeout==true is
 * NOT fed to the cache request path. The timeout master frame falls through to
 * the else branch (clear_pending), never to on_request (sniffer.c ~677).
 * ============================================================ */

void test_ws_dispatch_master_read_timeout_suppressed_all_fc(void)
{
    const uint8_t fcs[] = {0x01, 0x02, 0x03, 0x04};
    for (unsigned i = 0; i < sizeof(fcs); i++) {
        mock_cache_multimaster_reset();
        mock_cache_multimaster_enabled = true;
        sniffer_enable(0, SNIFF_REASON_CACHE);

        sniff_packet_t pkt = make_master_read_request(fcs[i], /*is_timeout=*/true);
        sniffer_ws_dispatch(&pkt);

        TEST_ASSERT_EQUAL_MESSAGE(0, mock_cache_multimaster_on_request_called,
            "a timed-out master read must NOT be fed into the cache request path");
        /* The timeout master frame is a transaction-ending event: it must clear the
         * pending request instead (else branch, corr-7). */
        TEST_ASSERT_EQUAL_MESSAGE(1, mock_cache_multimaster_clear_pending_called,
            "a timed-out master read must clear the pending request");
        TEST_ASSERT_EQUAL(0, mock_cache_multimaster_on_response_called);
    }
}

/* ============================================================
 * TC-WS-14: the NON-timeout equivalent of TC-WS-13 IS fed to on_request.
 * Same packet, is_timeout==false → on_request fires with decoded args, and the
 * pending request is NOT cleared. Locks in the timeout flag as the only
 * difference that suppresses the request feed.
 * ============================================================ */

void test_ws_dispatch_master_read_non_timeout_fed_all_fc(void)
{
    const uint8_t fcs[] = {0x01, 0x02, 0x03, 0x04};
    for (unsigned i = 0; i < sizeof(fcs); i++) {
        mock_cache_multimaster_reset();
        mock_cache_multimaster_enabled = true;
        sniffer_enable(0, SNIFF_REASON_CACHE);

        sniff_packet_t pkt = make_master_read_request(fcs[i], /*is_timeout=*/false);
        sniffer_ws_dispatch(&pkt);

        TEST_ASSERT_EQUAL_MESSAGE(1, mock_cache_multimaster_on_request_called,
            "a non-timeout master read must be fed into the cache request path");
        TEST_ASSERT_EQUAL(0, mock_cache_multimaster_on_request_last_port);
        TEST_ASSERT_EQUAL(7, mock_cache_multimaster_on_request_last_slave_id);
        TEST_ASSERT_EQUAL(fcs[i], mock_cache_multimaster_on_request_last_function);
        TEST_ASSERT_EQUAL(16, mock_cache_multimaster_on_request_last_start_reg);
        TEST_ASSERT_EQUAL(5, mock_cache_multimaster_on_request_last_count);
        /* A normal read request does not end the transaction → no clear_pending. */
        TEST_ASSERT_EQUAL_MESSAGE(0, mock_cache_multimaster_clear_pending_called,
            "a live read request must NOT clear the pending request");
    }
}

/* ============================================================
 * TC-WS-15: a SLAVE response with FC 0x01/0x02/0x03/0x04, valid CRC and
 * data_len within range (>= 5) is forwarded verbatim to
 * cache_multimaster_on_response() with the right slave/fc/data
 * (sniffer.c ~685-690).
 * ============================================================ */

void test_ws_dispatch_slave_response_fed_all_read_fc(void)
{
    const uint8_t fcs[] = {0x01, 0x02, 0x03, 0x04};
    for (unsigned i = 0; i < sizeof(fcs); i++) {
        mock_cache_multimaster_reset();
        mock_cache_multimaster_enabled = true;
        sniffer_enable(0, SNIFF_REASON_CACHE);

        /* Slave response: [slave][fc][byte_count][payload...][crc...]. data_len 7. */
        sniff_packet_t pkt = {0};
        pkt.port       = 0;
        pkt.slave_id   = 9;
        pkt.function   = fcs[i];
        pkt.is_master  = false;
        pkt.is_timeout = false;
        pkt.crc_valid  = true;
        pkt.timestamp_us = 0x1122334455667788ULL;
        pkt.data_len   = 7;
        pkt.data[0] = 9; pkt.data[1] = fcs[i]; pkt.data[2] = 0x02;
        pkt.data[3] = 0xDE; pkt.data[4] = 0xAD; pkt.data[5] = 0xBE; pkt.data[6] = 0xEF;

        sniffer_ws_dispatch(&pkt);

        TEST_ASSERT_EQUAL_MESSAGE(1, mock_cache_multimaster_on_response_called,
            "a valid slave read response must be fed to on_response");
        TEST_ASSERT_EQUAL(0, mock_cache_multimaster_on_response_last_port);
        TEST_ASSERT_EQUAL(9, mock_cache_multimaster_on_response_last_slave_id);
        TEST_ASSERT_EQUAL(fcs[i], mock_cache_multimaster_on_response_last_function);
        TEST_ASSERT_EQUAL(7, mock_cache_multimaster_on_response_last_data_len);
        TEST_ASSERT_EQUAL_HEX64(0x1122334455667788ULL,
            mock_cache_multimaster_on_response_last_timestamp_us);
        /* The exact packet bytes must be forwarded (right data). */
        TEST_ASSERT_FALSE_MESSAGE(mock_cache_multimaster_on_response_last_data_null,
            "on_response must receive a non-NULL data pointer");
        TEST_ASSERT_EQUAL_HEX8_ARRAY(pkt.data,
            mock_cache_multimaster_on_response_last_data, pkt.data_len);
        /* A valid response is not a transaction-ending exception → no clear_pending. */
        TEST_ASSERT_EQUAL(0, mock_cache_multimaster_clear_pending_called);
    }
}

/* ============================================================
 * TC-WS-16: a SLAVE response just below the minimum range (data_len < 5) is NOT
 * treated as a cacheable response; it falls through to the else branch
 * (clear_pending). Locks the data_len >= 5 lower bound of the response path.
 * ============================================================ */

void test_ws_dispatch_slave_response_below_min_len_not_fed(void)
{
    mock_cache_multimaster_enabled = true;
    sniffer_enable(0, SNIFF_REASON_CACHE);

    sniff_packet_t pkt = {0};
    pkt.port       = 0;
    pkt.slave_id   = 3;
    pkt.function   = 0x04;
    pkt.is_master  = false;
    pkt.is_timeout = false;
    pkt.crc_valid  = true;
    pkt.data_len   = 4;            /* one byte short of the >= 5 minimum */
    pkt.data[0] = 3; pkt.data[1] = 0x04; pkt.data[2] = 0x00; pkt.data[3] = 0x00;

    sniffer_ws_dispatch(&pkt);

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_cache_multimaster_on_response_called,
        "a too-short slave response must NOT be fed to on_response");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_cache_multimaster_clear_pending_called,
        "a too-short slave response ends the transaction → clear_pending");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_ws_dispatch_no_client);
    RUN_TEST(test_ws_dispatch_no_server);
    RUN_TEST(test_ws_dispatch_valid_client_sends);
    RUN_TEST(test_ws_dispatch_stale_fd_http_client_no_send);
    RUN_TEST(test_ws_dispatch_stale_fd_invalid_no_send);
    RUN_TEST(test_ws_dispatch_send_failure_clears_client);
    RUN_TEST(test_ws_dispatch_cache_request_min_len_8);
    RUN_TEST(test_ws_dispatch_cache_not_fed_without_reason);
    RUN_TEST(test_ws_dispatch_exception_clears_pending);
    RUN_TEST(test_ws_dispatch_timeout_clears_pending);
    RUN_TEST(test_ws_dispatch_valid_response_no_clear);
    RUN_TEST(test_ws_dispatch_master_write_clears_pending);
    RUN_TEST(test_ws_dispatch_master_read_timeout_suppressed_all_fc);
    RUN_TEST(test_ws_dispatch_master_read_non_timeout_fed_all_fc);
    RUN_TEST(test_ws_dispatch_slave_response_fed_all_read_fc);
    RUN_TEST(test_ws_dispatch_slave_response_below_min_len_not_fed);

    return UNITY_END();
}
