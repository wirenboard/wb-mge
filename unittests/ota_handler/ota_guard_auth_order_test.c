/* Binary 3 of the OTA state-guard suite: the guard sits after the authentication check.
 *
 * An unauthenticated caller must get the authentication answer and learn nothing about what the
 * device is doing. The test only means something once the guard is actually set — otherwise both
 * orders of the two checks pass it — so it first drives a successful upload to leave the handler in
 * pending_reboot, and only then sends the unauthenticated request.
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

void test_unauthenticated_request_gets_auth_answer_not_conflict(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test an unauthenticated request is answered by the auth check, not by the OTA guard");
    LOG_MESSAGE();

    /* Step 1: a successful upload, so the guard is set for the request that follows. */
    httpd_req_t first_req;
    ota_test_make_request(&first_req);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        ota_update_post_handler(&first_req),
        "The first update must succeed, otherwise the guard is not set and this test proves nothing"
    );

    /* Step 2: an unauthenticated request while the guard would otherwise answer 409. */
    mock_json_utils_reset();
    mock_auth_set_result(false);

    httpd_req_t unauth_req;
    ota_test_make_request(&unauth_req);

    esp_err_t result = ota_update_post_handler(&unauth_req);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        result,
        "A rejected authentication returns ESP_OK: the middleware has already answered the request"
    );
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "401 Unauthorized",
        mock_httpd_resp_last_status,
        "The caller must get the authentication status, not 409"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0,
        mock_json_utils_send_error_called,
        "The OTA guard must not answer an unauthenticated request at all"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_esp_ota_begin_call_count,
        "The unauthenticated request must not start an update"
    );
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_unauthenticated_request_gets_auth_answer_not_conflict);

    return UNITY_END();
}
