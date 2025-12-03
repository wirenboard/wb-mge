#include "unity.h"
#include "console_log.h"

#include "array_size.h"
#include "esp_http_server.h"
#include "http_server.h"
#include "setting_items.h"

#include <string.h>

#define WEB_PORT                            8080

void mock_setting_items_set_web_port(int port);

void setUp(void)
{
    esp_http_server_init();
    mock_handlers_reset();
}

void tearDown(void)
{

}

void test_http_server_deinit_not_initialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP server deinitialization when HTTP server is not initialized");
    LOG_MESSAGE();

    esp_err_t result = http_server_deinit();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        result,
        "HTTP server deinitialization should return ESP_OK when HTTP server is not initialized"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0,
        mock_httpd_stop_call_count,
        "httpd_stop() should not be called when HTTP server is not initialized"
    );
}

void test_http_server_deinit_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP server deinitialization in normal conditions");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(WEB_PORT);
    esp_err_t result = http_server_init();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        result,
        "HTTP server initialization should succeed"
    );

    result = http_server_deinit();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        result,
        "HTTP server deinitialization should return ESP_OK"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_httpd_stop_call_count,
        "httpd_stop() must be called once"
    );

    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        mock_server_handle,
        mock_captured_httpd_stop_handle,
        "Handle used in httpd_stop() call must be equal to the handle of initialized HTTP server"
    );
}

void test_http_server_deinit_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP server deinitialization when httpd_stop() fails");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(WEB_PORT);
    esp_err_t result = http_server_init();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        result,
        "HTTP server initialization should succeed"
    );

    mock_httpd_stop_return_value = ESP_ERR_INVALID_STATE;
    result = http_server_deinit();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_ERR_INVALID_STATE,
        result,
        "HTTP server deinitialization should return the same error code that returns httpd_stop()"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_httpd_stop_call_count,
        "httpd_stop() must be called once"
    );

    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        mock_server_handle,
        mock_captured_httpd_stop_handle,
        "Handle used in httpd_stop() call must be equal to the handle of initialized HTTP server"
    );
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_http_server_deinit_not_initialized);
    RUN_TEST(test_http_server_deinit_success);
    RUN_TEST(test_http_server_deinit_fail);

    return UNITY_END();
}
