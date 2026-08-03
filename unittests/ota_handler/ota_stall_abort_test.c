/* Binary 5 of the OTA suite: an upload whose client went silent is given up on after the stall
 * limit, instead of being retried forever.
 *
 * A client that vanishes without closing its socket (laptop suspended, phone left the AP) sends
 * neither FIN nor RST, and with TCP keep-alive off httpd_req_recv() can only ever report timeouts.
 * The IDF web server is single-threaded, so a receive loop that keeps retrying takes every other
 * endpoint down with it until the device is power-cycled — the only thing that ends it is the
 * counter in ota_receive_and_write().
 *
 * One test per binary: the state guard in ota_handler.c is a file-static that no test can reset. */

#include "unity.h"
#include "console_log.h"

#include "esp_http_server.h"
#include "http_server.h"
#include "ota_handler.h"
#include "ota_test_env.h"

/* Same derivation as OTA_RECV_MAX_STALL_TIMEOUTS in main/ota_handler.c: the 30 s silence budget
 * divided by the configured receive window. Spelled out again instead of being imported, because
 * the limit is private to the module under test and the point of the test is to pin the number of
 * retries the handler actually performs. */
#define EXPECTED_MAX_STALL_TIMEOUTS     (30000 / (HTTP_RECV_WAIT_TIMEOUT_S * 1000))

/* Enough for the wb_app_desc the handler validates out of the first chunk (480 bytes), small enough
 * that the 1 KB body needs more than one receive. */
#define STALL_TEST_CHUNK_LEN            512

void setUp(void)
{
    ota_test_env_reset();
}

void tearDown(void)
{
}

void test_endless_receive_timeouts_abort_the_upload(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test an upload receiving nothing but timeouts is aborted after the stall limit");
    LOG_MESSAGE();

    /* The client is gone from the very first read: the socket is open and completely silent. */
    mock_http_set_recv_error_always(HTTPD_SOCK_ERR_TIMEOUT);

    httpd_req_t req;
    ota_test_make_request(&req);

    esp_err_t result = ota_update_post_handler(&req);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        EXPECTED_MAX_STALL_TIMEOUTS,
        mock_httpd_req_recv_call_count,
        "The handler must stop asking after the stall limit; without the counter it would retry forever "
        "and block the whole web server"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_ota_written_bytes, "Nothing was received, so nothing must be written");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_esp_ota_abort_call_count,
        "The abandoned upload must release the partition with esp_ota_abort()"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0,
        mock_esp_ota_set_boot_partition_call_count,
        "No image was written, so the boot partition must not be switched"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_json_utils_send_error_called, "The failure must be reported");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "Network timeout during upload",
        mock_json_utils_last_error,
        "The error must name the stall, not a generic failure"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "A reported failure returns whatever the error response returns");
}

void test_a_partial_upload_that_goes_silent_is_aborted_too(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test an upload that starts fine and then goes silent is aborted after the stall limit");
    LOG_MESSAGE();

    /* The realistic shape of the bug: the client sends the head of the image and disappears. The
     * accepted bytes must not buy the dead connection any extra retries. */
    static int script[1 + EXPECTED_MAX_STALL_TIMEOUTS];
    script[0] = MOCK_RECV_DATA;
    for (size_t i = 1; i < (sizeof(script) / sizeof(script[0])); i++) {
        script[i] = HTTPD_SOCK_ERR_TIMEOUT;
    }

    mock_http_set_recv_chunk_limit(STALL_TEST_CHUNK_LEN);
    mock_http_set_recv_script(script, sizeof(script) / sizeof(script[0]));

    httpd_req_t req;
    ota_test_make_request(&req);

    esp_err_t result = ota_update_post_handler(&req);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1 + EXPECTED_MAX_STALL_TIMEOUTS,
        mock_httpd_req_recv_call_count,
        "One accepted chunk plus the stall limit: the handler must give up on the silence that follows"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        STALL_TEST_CHUNK_LEN,
        mock_esp_ota_written_bytes,
        "Only the chunk that actually arrived may be written"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_esp_ota_abort_call_count, "The half-written image must be aborted");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0,
        mock_esp_ota_set_boot_partition_call_count,
        "A partial image must never become the boot partition"
    );
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "Network timeout during upload",
        mock_json_utils_last_error,
        "The error must name the stall"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "A reported failure returns whatever the error response returns");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_endless_receive_timeouts_abort_the_upload);
    RUN_TEST(test_a_partial_upload_that_goes_silent_is_aborted_too);

    return UNITY_END();
}
