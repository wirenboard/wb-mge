/* Binary 1 of the OTA state-guard suite: a second POST /update after a successful one is refused
 * with 409 and never touches the flash.
 *
 * This is the race the guard exists for. On the device the first request ends with the boot
 * partition already switched and a reboot scheduled a second later; a request arriving inside that
 * second would call esp_ota_begin(), which erases the whole target partition — the very image the
 * device is about to boot. Nothing bricks (the target is always the slot the firmware is not
 * running from, and the bootloader falls back to the previous image), but the update silently does
 * not happen and the device comes back up on the old firmware.
 *
 * One test per binary: the guard is a file-static that is never reset, so the successful first
 * upload here would poison any test that ran after it. */

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

void test_second_update_before_reboot_is_refused(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test a second POST /update before the reboot gets 409 and does not erase the written image");
    LOG_MESSAGE();

    /* Step 1: a normal, successful upload. It leaves the device with the image written, the boot
     * partition switched and a reboot pending. */
    httpd_req_t first_req;
    ota_test_make_request(&first_req);

    esp_err_t first_result = ota_update_post_handler(&first_req);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, first_result, "A successful update must return ESP_OK");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_esp_ota_begin_call_count, "esp_ota_begin() must be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        OTA_TEST_BODY_LEN,
        mock_esp_ota_written_bytes,
        "The whole body must be written to flash"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_esp_ota_set_boot_partition_call_count,
        "The boot partition must be switched to the freshly written image"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_json_utils_send_response_called, "A success response must be sent");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_json_utils_send_error_called, "No error response must be sent");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_cmd_reboot_device_call_count, "A reboot must be scheduled");

    /* Step 2: the second upload, arriving before that reboot. Only its effects matter from here. */
    mock_json_utils_reset();
    mock_cmd_handler_reset();

    httpd_req_t second_req;
    ota_test_make_request(&second_req);

    esp_err_t second_result = ota_update_post_handler(&second_req);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_FAIL,
        second_result,
        "The refusal must return ESP_FAIL: on ESP_OK the server would read and discard the unsent "
        "firmware body in 32-byte chunks, blocking every other endpoint while doing it"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_esp_ota_begin_call_count,
        "esp_ota_begin() must NOT be called again: it would erase the image the device is about to boot"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        OTA_TEST_BODY_LEN,
        mock_esp_ota_written_bytes,
        "Nothing more must be written to flash"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_json_utils_send_error_called, "An error response must be sent");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "409 Conflict",
        mock_json_utils_last_error_status,
        "The refusal must carry HTTP status 409"
    );
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "OTA update already in progress",
        mock_json_utils_last_error,
        "The refusal must explain itself"
    );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0,
        mock_json_utils_send_response_called,
        "No success response must be sent for a refused update"
    );
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_second_update_before_reboot_is_refused);

    return UNITY_END();
}
