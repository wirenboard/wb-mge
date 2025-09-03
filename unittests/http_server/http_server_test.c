#include "unity.h"
#include "console_log.h"

#include "http_server.h"
#include "esp_http_server.h"
#include "setting_items.h"

#include <string.h>

#define MAX_URI_HANDLERS                    20
#define STACK_SIZE                          1024 * 6
#define MAX_OPEN_SOCKETS                    12

void mock_setting_items_set_web_port(int port);

void setUp(void)
{
    esp_http_server_init();
}

void tearDown(void)
{

}

// Тестируем, что при инициализации HTTP сервера используется заданный порт, а также вызываются необходимые функции
void test_http_server_init_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP server initialization - success case");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(8080);

    esp_err_t result = http_server_init();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        result,
        "HTTP server initialization should succeed"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_wifi_scan_init_call_count,
        "wifi_scan_init should be called once"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_auth_init_call_count,
        "auth_init should be called once"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_httpd_start_call_count,
        "httpd_start should be called once"
    );

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        8080,
        mock_captured_config.server_port,
        "HTTP server should use configured port"
    );
}

// Тестируем, что при задании порта 0 используется порт по умолчанию (80)
void test_http_server_init_default_port_fallback(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP server initialization - default port fallback");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(0);

    esp_err_t result = http_server_init();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        result,
        "HTTP server initialization should succeed with default port"
    );

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        80,
        mock_captured_config.server_port,
        "HTTP server should use default port 80 when configured port is 0"
    );
}

// Тестируем, что при неудачной инициализации WiFi модуля HTTP сервер также инициализируется неудачно
void test_http_server_init_wifi_scan_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP server initialization - WiFi scan init failure");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(8080);
    mock_wifi_scan_init_return_value = ESP_FAIL;

    esp_err_t result = http_server_init();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_FAIL,
        result,
        "HTTP server initialization should fail when WiFi scan init fails"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_wifi_scan_init_call_count,
        "wifi_scan_init should be called once"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0,
        mock_httpd_start_call_count,
        "httpd_start should not be called when wifi_scan_init fails"
    );
}

// Тестируем, что при неудачной инициализации модуля аутентификации HTTP сервер также инициализируется неудачно
void test_http_server_init_auth_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP server initialization - auth init failure");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(8080);
    mock_auth_init_return_value = ESP_FAIL;

    esp_err_t result = http_server_init();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_FAIL,
        result,
        "HTTP server initialization should fail when auth init fails"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_auth_init_call_count,
        "auth_init should be called once"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0,
        mock_httpd_start_call_count,
        "httpd_start should not be called when auth_init fails"
    );
}

// Тестируем, что при неудачной инициализации модуля httpd_start HTTP сервер также инициализируется неудачно
void test_http_server_init_httpd_start_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP server initialization - httpd_start failure");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(8080);
    mock_httpd_start_return_value = ESP_FAIL;

    esp_err_t result = http_server_init();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_FAIL,
        result,
        "HTTP server initialization should fail when httpd_start fails"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_wifi_scan_init_call_count,
        "wifi_scan_init should be called once"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_auth_init_call_count,
        "auth_init should be called once"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1,
        mock_httpd_start_call_count,
        "httpd_start should be called once"
    );
}

// Тестируем установку конфигурационных параметров HTTP сервера при инициализации
void test_http_server_config_parameters(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP server configuration parameters");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(3000);

    esp_err_t result = http_server_init();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        result,
        "HTTP server initialization should succeed"
    );

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        MAX_URI_HANDLERS,
        mock_captured_config.max_uri_handlers,
        "max_uri_handlers should be set to the default value"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        STACK_SIZE,
        mock_captured_config.stack_size,
        "stack_size should be set to the default value"
    );

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        MAX_OPEN_SOCKETS,
        mock_captured_config.max_open_sockets,
        "max_open_sockets should be set to the default value"
    );

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        3000,
        mock_captured_config.server_port,
        "server_port should be set to configured value"
    );
}

// Тестируем регистрацию обработчиков URI при инициализации HTTP сервера
void test_http_server_uri_handlers_registration(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP server URI handlers registration");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(8080);

    esp_err_t result = http_server_init();

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        result,
        "HTTP server initialization should succeed"
    );

    const char* expected_uris[] = {
        "/auth",
        "/session",
        "/logout",
        "/",
        "/index.css",
        "/index.js",
        "/favicon.webp",
        "/update",
        "/info",
        "/info",
        "/settings",
        "/settings",
        "/cmd",
        "/wifi_scan/start",
        "/wifi_scan/results",
        "/ap_clients",
        "/uptime"
    };

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        17,
        mock_httpd_register_uri_handler_call_count,
        "All expected URI handlers should be registered"
    );

    for (size_t i = 0; i < 17; i++) {
        bool found = false;
        for (int j = 0; j < mock_httpd_register_uri_handler_call_count; j++) {
            if (strcmp(mock_registered_uris[j], expected_uris[i]) == 0) {
                found = true;
                break;
            }
        }

        char message[100];
        snprintf(message, sizeof(message), "URI '%s' should be registered", expected_uris[i]);
        TEST_ASSERT_TRUE_MESSAGE(found, message);
    }
}

// Тестируем граничные значения портов при инициализации HTTP сервера
void test_http_server_port_edge_cases(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP server port edge cases");
    LOG_MESSAGE();

    const int test_ports[] = {1, 80, 443, 8080, 65535};
    const size_t num_ports = sizeof(test_ports) / sizeof(test_ports[0]);

    for (size_t i = 0; i < num_ports; i++) {
        esp_http_server_init();

        mock_setting_items_set_web_port(test_ports[i]);

        esp_err_t result = http_server_init();

        TEST_ASSERT_EQUAL_INT_MESSAGE(
            ESP_OK,
            result,
            "HTTP server initialization should succeed for all valid ports"
        );

        TEST_ASSERT_EQUAL_UINT16_MESSAGE(
            test_ports[i],
            mock_captured_config.server_port,
            "HTTP server should use the configured port"
        );
    }
}

// Тестируем множественные вызовы инициализации HTTP сервера
void test_http_server_multiple_init_calls(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP server multiple initialization calls");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(8080);

    esp_err_t result1 = http_server_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result1, "First initialization should succeed");

    int first_call_count = mock_httpd_start_call_count;

    esp_err_t result2 = http_server_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result2, "Second initialization should succeed");

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        first_call_count + 1,
        mock_httpd_start_call_count,
        "httpd_start should be called again on second init"
    );
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_http_server_init_success);
    RUN_TEST(test_http_server_init_default_port_fallback);
    RUN_TEST(test_http_server_init_wifi_scan_failure);
    RUN_TEST(test_http_server_init_auth_failure);
    RUN_TEST(test_http_server_init_httpd_start_failure);
    RUN_TEST(test_http_server_config_parameters);
    RUN_TEST(test_http_server_uri_handlers_registration);
    RUN_TEST(test_http_server_port_edge_cases);
    RUN_TEST(test_http_server_multiple_init_calls);

    return UNITY_END();
}
