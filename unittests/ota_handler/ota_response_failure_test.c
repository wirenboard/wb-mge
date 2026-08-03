/* Binary 4 of the OTA state-guard suite: the "firmware written, response could not be built" branch.
 *
 * The image is valid and the boot partition is already switched when the success response is built,
 * so a failure to allocate that JSON must not swallow the update: the device has to reboot anyway
 * and come up on the new firmware. The same branch must leave the guard set — observed here the
 * only way it can be, by the 409 the next request gets, since the flag has no accessor.
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

void test_reboot_happens_even_if_the_success_response_cannot_be_built(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test a written image still reboots when the success response cannot be built");
    LOG_MESSAGE();

    /* The heap is exhausted exactly when the success response is allocated. */
    mock_cjson_set_create_object_fails(true);

    httpd_req_t req;
    ota_test_make_request(&req);

    esp_err_t result = ota_update_post_handler(&req);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_esp_ota_set_boot_partition_call_count,
        "The image must have been written and the boot partition switched"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_cmd_reboot_device_call_count,
        "The device must reboot anyway: the firmware is valid and the boot partition already points at it"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_json_utils_send_error_called,
        "The caller must be told the response could not be built"
    );
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "Failed to create response",
        mock_json_utils_last_error,
        "The error must name the failure that actually happened"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        result,
        "The response-build failure returns whatever the error response returns, as before the guard"
    );

    /* The update is pending a reboot, so the next request must be refused — this is the only way to
     * observe the guard state from outside. */
    mock_json_utils_reset();

    httpd_req_t next_req;
    ota_test_make_request(&next_req);

    esp_err_t next_result = ota_update_post_handler(&next_req);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_FAIL,
        next_result,
        "The refusal must return ESP_FAIL so the server closes the socket instead of draining the body"
    );
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "409 Conflict",
        mock_json_utils_last_error_status,
        "A written-but-not-yet-booted image must refuse the next update with 409"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_esp_ota_begin_call_count,
        "The refused request must not erase the written image"
    );
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_reboot_happens_even_if_the_success_response_cannot_be_built);

    return UNITY_END();
}
