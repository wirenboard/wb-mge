/* Binary 6 of the OTA suite: timeouts that are interrupted by accepted data must not add up.
 *
 * The counterpart of ota_stall_abort_test. A link that is alive but bad (Wi-Fi at the edge of range)
 * produces timeouts too — many more of them than the limit, over a long upload. What separates it
 * from a dead client is that data keeps arriving in between, so the counter has to be cleared by
 * every accepted chunk. Without that reset the limit would count timeouts for the whole transfer
 * and kill exactly the slow uploads it was meant to protect.
 *
 * One test per binary: this upload succeeds and leaves the state guard pending a reboot, which would
 * decide the outcome of anything running after it. */

#include "unity.h"
#include "console_log.h"

#include "esp_http_server.h"
#include "http_server.h"
#include "ota_handler.h"
#include "ota_test_env.h"

/* Same derivation as OTA_RECV_MAX_STALL_TIMEOUTS in main/ota_handler.c: the 30 s silence budget
 * divided by the configured receive window. Spelled out again instead of being imported, because
 * the limit is private to the module under test. */
#define EXPECTED_MAX_STALL_TIMEOUTS     (30000 / (HTTP_RECV_WAIT_TIMEOUT_S * 1000))

/* One short of the limit: the longest silence a live-but-bad link may produce here. */
#define STALL_BURST_LEN                 (EXPECTED_MAX_STALL_TIMEOUTS - 1)

/* Enough for the wb_app_desc the handler validates out of the first chunk (480 bytes), and exactly
 * half the body, so the upload takes two accepted receives with a burst of timeouts before each. */
#define STALL_TEST_CHUNK_LEN            (OTA_TEST_BODY_LEN / 2)

#define STALL_TEST_CHUNKS               (OTA_TEST_BODY_LEN / STALL_TEST_CHUNK_LEN)

void setUp(void)
{
    ota_test_env_reset();
}

void tearDown(void)
{
}

void test_timeouts_between_accepted_chunks_do_not_abort_the_upload(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test timeouts interleaved with accepted data never reach the stall limit");
    LOG_MESSAGE();

    /* Bursts of timeouts, each one short of the limit, separated by an accepted chunk. In total the
     * upload sees far more timeouts than the limit, and none of it may matter. */
    static int script[STALL_TEST_CHUNKS * (STALL_BURST_LEN + 1)];
    size_t entries = 0;
    for (int chunk = 0; chunk < STALL_TEST_CHUNKS; chunk++) {
        for (int i = 0; i < STALL_BURST_LEN; i++) {
            script[entries++] = HTTPD_SOCK_ERR_TIMEOUT;
        }
        script[entries++] = MOCK_RECV_DATA;
    }

    mock_http_set_recv_chunk_limit(STALL_TEST_CHUNK_LEN);
    mock_http_set_recv_script(script, entries);

    httpd_req_t req;
    ota_test_make_request(&req);

    esp_err_t result = ota_update_post_handler(&req);

    TEST_ASSERT_TRUE_MESSAGE(
        (STALL_TEST_CHUNKS * STALL_BURST_LEN) > EXPECTED_MAX_STALL_TIMEOUTS,
        "The scenario is only meaningful if the upload saw more timeouts than the limit allows in a row"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "A completed upload must succeed despite the timeouts along the way");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        OTA_TEST_BODY_LEN,
        mock_esp_ota_written_bytes,
        "The whole body must be written: every accepted chunk clears the timeout counter"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_ota_abort_call_count, "Nothing was abandoned, so nothing must be aborted");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_esp_ota_set_boot_partition_call_count,
        "The boot partition must be switched to the freshly written image"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_json_utils_send_error_called, "No error response must be sent");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_json_utils_send_response_called, "The upload must be answered with success");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_cmd_reboot_device_call_count, "A reboot must be scheduled");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_timeouts_between_accepted_chunks_do_not_abort_the_upload);

    return UNITY_END();
}
