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

void test_check_settings_changed_not_initialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP server settings change check function when HTTP server is not initialized");
    LOG_MESSAGE();

    bool result = http_server_check_settings_changed();

    TEST_ASSERT_FALSE_MESSAGE(
        result,
        "http_server_check_settings_changed() should return 'false' when HTTP server is not initialized"
    );
}

void test_check_settings_changed_no_changes(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP server settings change check function when settings not changed");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(WEB_PORT);
    esp_err_t result = http_server_init();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        result,
        "HTTP server initialization should succeed"
    );

    bool changed = http_server_check_settings_changed();

    TEST_ASSERT_FALSE_MESSAGE(
        changed,
        "http_server_check_settings_changed() should return 'false' when settings not changed"
    );
}

void test_check_settings_changed_true(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP server settings change check function when settings were changed");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(WEB_PORT);
    esp_err_t result = http_server_init();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        result,
        "HTTP server initialization should succeed"
    );

    mock_setting_items_set_web_port(WEB_PORT + 1);
    bool changed = http_server_check_settings_changed();

    TEST_ASSERT_TRUE_MESSAGE(
        changed,
        "http_server_check_settings_changed() should return 'true' when settings were changed"
    );
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_check_settings_changed_not_initialized);
    RUN_TEST(test_check_settings_changed_no_changes);
    RUN_TEST(test_check_settings_changed_true);

    return UNITY_END();
}
