/*
 * sniffer_guards_test.c — the sniffer must degrade, not panic, while it is not up
 *
 * Every other test file in this directory calls sniffer_init() in setUp() and works
 * with a healthy module. This one does the opposite: it drives the public entry points
 * of a sniffer whose FreeRTOS handles do not exist, which is the state the device is in
 * between http_server_init() and the sniffer coming up, and the state it stays in for
 * good when sniffer_init() runs out of memory and main.c carries on booting anyway.
 *
 * Reaching a NULL queue / timer / semaphore handle is not a soft failure on the device:
 * FreeRTOS opens those calls with configASSERT(), so it is a panic and a reboot. The
 * FreeRTOS mocks reproduce that verdict — xTimerStop(), xQueueSend() and xQueueReceive()
 * fail the test on a NULL handle — so removing any guard under test here turns the
 * corresponding case red rather than letting it pass quietly.
 *
 * TC-G1  sniffer_attach()  before init — the RX callback is not published
 * TC-G2  sniffer_enable()  before init — the port is not armed for the cache overlay
 * TC-G3  sniffer_disable() with the handles gone — no xTimerStop() on a NULL timer,
 *        and the reason bit is dropped anyway
 * TC-G4  RX path with the handles gone — no queue send, no timer call
 * TC-G5  /sniffer/ws before init — 503, and no WS client is registered
 */

#include "unity.h"
#include "console_log.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "sniffer.h"
#include "serial_mock.h"

#include "freertos/queue.h"
#include "freertos/timers.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/* esp_http_server mock controls (provides httpd_req_t used below) */
#include "esp_http_server.h"

#include "bridge.h"

/* Internals under test — static in production, external in __unittest_env__ builds. */
void      sniffer_ws_dispatch(sniff_packet_t *pkt);
esp_err_t sniffer_ws_handler(httpd_req_t *req);
int       sniffer_test_get_ws_client_fd(void);

/* cache_multimaster recording mock controls */
extern bool mock_cache_multimaster_enabled;
extern void mock_cache_multimaster_reset(void);
extern int  mock_cache_multimaster_on_response_called;

/* ============================================================
 * Fixtures
 * ============================================================ */

static serial_desc_t s_desc0;

#define SEND0(arr) s_desc0.sniff_handler(NULL, (arr), sizeof(arr))

static void reset_all_mocks(void)
{
    mock_freertos_queue_reset();
    mock_freertos_timers_reset();
    mock_freertos_task_reset();
    mock_freertos_semaphore_reset();
    mock_serial_reset();
    mock_esp_http_server_reset();
    mock_cache_multimaster_reset();
}

/* ============================================================
 * setUp / tearDown
 * ============================================================ */

void setUp(void)
{
    reset_all_mocks();
    memset(&s_desc0, 0, sizeof(s_desc0));

    /* Put the module back to "not up" regardless of what the previous test left
     * behind — the handles are file statics and there is no deinit API, so this takes
     * two inits:
     *   1) a successful one, the only thing in the module that resets the per-port
     *      bookkeeping (reasons, framing state, pending request);
     *   2) one that fails at the queue, whose sniffer_init_cleanup() deletes and NULLs
     *      every handle again — including the timers the first init created.
     * The result is the same state a device has before its very first sniffer_init(),
     * and every test below starts from it no matter the order they run in. */
    (void)sniffer_init();
    mock_xQueueCreate_data.should_fail = true;
    (void)sniffer_init();
    mock_xQueueCreate_data.should_fail = false;

    /* The two inits are fixture, not part of any test — clear what they recorded. */
    reset_all_mocks();
}

void tearDown(void) {}

/* Bring the sniffer up on port 0, then take its handles away again, leaving the
 * per-port bookkeeping saying the port is sniffing.
 *
 * This is a state, not a boot sequence: port_manager_init_subsystems() allows exactly
 * one attempt, so production never re-inits the sniffer. It is constructed here because
 * it is precisely the precondition the handle checks exist for — "the bitmask says this
 * port wants sniffing, the handle it needs is gone" — and a failing re-init is the only
 * way to reach it from a host test. */
static void take_the_handles_away(void)
{
    mock_xQueueCreate_data.should_fail = true;
    TEST_ASSERT_NOT_EQUAL_MESSAGE(ESP_OK, sniffer_init(),
        "a queue-create failure must make sniffer_init() fail");
    mock_xQueueCreate_data.should_fail = false;

    /* Counters only — the handles must stay NULL. */
    mock_freertos_timers_reset();
    mock_freertos_queue_reset();
    mock_freertos_semaphore_reset();
}

/* ============================================================
 * TC-G1 — sniffer_attach() before init must not publish the RX callback
 * ============================================================ */

void test_attach_before_init_does_not_publish_rx_callback(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-G1: attach on a sniffer that is not up must leave the UART path alone");
    LOG_MESSAGE();

    sniffer_attach(0, &s_desc0);

    TEST_ASSERT_NULL_MESSAGE(s_desc0.sniff_handler,
        "publishing the callback would let every RX buffer walk into a NULL queue/timer");
}

/* ============================================================
 * TC-G2 — sniffer_enable() before init must not arm the port
 *
 * The reasons bitmask is what un-gates the RX path and what tells the cache overlay a
 * port's packets are wanted. Probed here through sniffer_ws_dispatch(), the only reader
 * of the bitmask a host test can call directly.
 * ============================================================ */

void test_enable_before_init_leaves_port_out_of_the_cache_path(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-G2: enable on a sniffer that is not up must not mark the port as sniffing");
    LOG_MESSAGE();

    mock_cache_multimaster_enabled = true;
    sniffer_enable(0, SNIFF_REASON_CACHE);

    /* An FC03 slave response — what the cache stores when the port's CACHE reason is set. */
    sniff_packet_t pkt = {0};
    pkt.port      = 0;
    pkt.is_master = false;
    pkt.crc_valid = true;
    pkt.slave_id  = 0x83;
    pkt.function  = 0x03;
    pkt.data_len  = 9;
    sniffer_ws_dispatch(&pkt);

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_cache_multimaster_on_response_called,
        "a refused enable must leave the port out of the cache path, not half-armed");
}

/* ============================================================
 * TC-G3 — sniffer_disable() must not stop a timer that no longer exists,
 *         and must clear the reason bit all the same
 *
 * The two halves are one test on purpose: they are the two ways the same line can be
 * got wrong. A guard placed ahead of the bitmask update protects the timer just as
 * well — and strands the port as "sniffing" forever, because sniffer_detach() clears a
 * port through this very function.
 * ============================================================ */

void test_disable_with_handles_gone_does_not_stop_null_timer(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-G3: the last reason going away must not xTimerStop() a NULL handle");
    LOG_MESSAGE();

    TEST_ASSERT_EQUAL(ESP_OK, sniffer_init());
    sniffer_attach(0, &s_desc0);
    sniffer_enable(0, SNIFF_REASON_CACHE);
    take_the_handles_away();

    /* Dropping the last reason is the branch that quiesces the port with
     * xTimerStop(ctx->resp_timer) — the handle is NULL by now. */
    sniffer_disable(0, SNIFF_REASON_CACHE);

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xTimerStop_called,
        "there is no timer left to stop — the pipeline it belonged to is gone with it");

    /* The bit itself is plain memory and has nothing to do with the missing handle, so
     * it must be gone regardless. Probed through sniffer_ws_dispatch(), the only reader
     * of the bitmask a host test can call directly (same probe as TC-G2). */
    mock_cache_multimaster_enabled = true;
    sniff_packet_t pkt = {0};
    pkt.port      = 0;
    pkt.is_master = false;
    pkt.crc_valid = true;
    pkt.slave_id  = 0x83;
    pkt.function  = 0x03;
    pkt.data_len  = 9;
    sniffer_ws_dispatch(&pkt);

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_cache_multimaster_on_response_called,
        "a disabled port must leave the cache path even when its timers are already gone");
}

/* ============================================================
 * TC-G4 — bus traffic arriving after the handles are gone is a silent no-op
 *
 * The port is left in RES_WAIT on purpose: every decision taken in that state begins by
 * disarming the response timer, so an unguarded RX path reaches xTimerStop() on the very
 * first frame — before it would reach the queue.
 * ============================================================ */

void test_rx_path_with_handles_gone_is_inert(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-G4: a frame from the bus must not reach a NULL timer or a NULL queue");
    LOG_MESSAGE();

    TEST_ASSERT_EQUAL(ESP_OK, sniffer_init());
    sniffer_attach(0, &s_desc0);
    sniffer_enable(0, SNIFF_REASON_DISPLAY);
    TEST_ASSERT_NOT_NULL_MESSAGE(s_desc0.sniff_handler,
        "attach on a healthy sniffer must publish the RX callback");

    /* FC03 request: emitted as MASTER, latched, response timer armed → RES_WAIT. */
    uint8_t req[] = {0x83, 0x03, 0x00, 0x61, 0x00, 0x02, 0x8B, 0xF7};
    SEND0(req);
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xTimerStart_called,
        "the request must arm the response timer, leaving the port in RES_WAIT");

    take_the_handles_away();

    /* The matching response arrives on a port whose pipeline no longer exists. */
    uint8_t resp[] = {0x83, 0x03, 0x04, 0x00, 0x03, 0x00, 0x1E, 0x28, 0x33};
    SEND0(resp);

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xTimerStop_called,
        "the RX path must not disarm a timer that was deleted");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xTimerStart_called,
        "nor arm one");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xQueueSend_data.called,
        "nor push the packet into a queue that no longer exists");
}

/* ============================================================
 * TC-G5 — /sniffer/ws before init answers 503 instead of touching a NULL mutex
 * ============================================================ */

void test_ws_handler_before_init_reports_503(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-G5: a WS request that beats the sniffer gets 503, not a panic");
    LOG_MESSAGE();

    httpd_req_t req = {
        .handle = (httpd_handle_t)0xCAFE,
        .method = HTTP_GET,
        .aux    = (void *)(intptr_t)42,
    };

    TEST_ASSERT_EQUAL(ESP_OK, sniffer_ws_handler(&req));

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_httpd_resp_set_status_called,
        "the caller must be told the sniffer is unavailable, not silently ignored");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("503 Service Unavailable",
        mock_httpd_resp_set_status_last,
        "the state is temporary/degraded — a 4xx would blame the request");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_httpd_resp_send_called, "an explanatory body is expected");

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphoreTake_called,
        "the handler must not take the WS mutex it just found missing");
    TEST_ASSERT_EQUAL_MESSAGE(-1, sniffer_test_get_ws_client_fd(),
        "a refused upgrade must not be registered as the WS client");
}

/* ============================================================
 * main
 * ============================================================ */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_attach_before_init_does_not_publish_rx_callback);
    RUN_TEST(test_enable_before_init_leaves_port_out_of_the_cache_path);
    RUN_TEST(test_disable_with_handles_gone_does_not_stop_null_timer);
    RUN_TEST(test_rx_path_with_handles_gone_is_inert);
    RUN_TEST(test_ws_handler_before_init_reports_503);

    return UNITY_END();
}
