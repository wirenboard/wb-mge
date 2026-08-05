/* Binary 2 of the OTA state-guard suite: a failed upload must not leave the guard stuck.
 *
 * The guard refuses updates while one is running or pending a reboot. Everything that ends without
 * a written image has to put it back to idle, otherwise a single dropped connection would make the
 * device permanently unflashable — and dropped connections are the common case, not the exotic one.
 *
 * One test per binary: the guard is a file-static that no test can reset from outside. */

#include "unity.h"
#include "console_log.h"

#include "esp_http_server.h"
#include "ota_handler.h"
#include "ota_test_env.h"

void setUp(void)
{
    ota_test_env_reset();
}

void tearDown(void)
{
}

void test_retry_after_failed_update_is_accepted(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test an upload that fails mid-transfer leaves the guard idle for the next attempt");
    LOG_MESSAGE();

    /* Step 1: the connection dies on the first read, the way an aborted upload does. */
    mock_http_set_recv_error(1, HTTPD_SOCK_ERR_FAIL);

    httpd_req_t failed_req;
    ota_test_make_request(&failed_req);

    esp_err_t failed_result = ota_update_post_handler(&failed_req);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        failed_result,
        "A failed upload answers with an error response and returns ESP_OK, as it did before the guard"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_esp_ota_begin_call_count, "The first attempt must have started an update");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_esp_ota_abort_call_count,
        "The failed upload must release the partition with esp_ota_abort()"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_ota_set_boot_partition_call_count, "No image was written, so no boot switch");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_json_utils_send_error_called, "The failure must be reported");

    /* Step 2: retry on a healthy connection. */
    ota_test_env_reset();

    httpd_req_t retry_req;
    ota_test_make_request(&retry_req);

    esp_err_t retry_result = ota_update_post_handler(&retry_req);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, retry_result, "The retry after a failed upload must succeed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_esp_ota_begin_call_count,
        "The retry must reach esp_ota_begin() again: a failed update must not leave the guard set"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0,
        mock_json_utils_send_error_called,
        "The retry must not be refused with an error response"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_json_utils_send_response_called, "The retry must be answered with success");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_cmd_reboot_device_call_count, "The retry must schedule the reboot");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_retry_after_failed_update_is_accepted);

    return UNITY_END();
}
