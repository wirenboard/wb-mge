#include "unity.h"
#include "console_log.h"

#include "http_server.h"
#include "esp_http_server.h"
#include "setting_items.h"

#include <string.h>

#define WEB_PORT                            8080
#define URI_HANDLERS_COUNT                  17

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

    mock_setting_items_set_web_port(WEB_PORT);

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
        WEB_PORT,
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
        WEB_PORT_DEFAULT,
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

    mock_setting_items_set_web_port(WEB_PORT);
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

    mock_setting_items_set_web_port(WEB_PORT);
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

    mock_setting_items_set_web_port(WEB_PORT);
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

    mock_setting_items_set_web_port(WEB_PORT);

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
        URI_HANDLERS_COUNT,
        mock_httpd_register_uri_handler_call_count,
        "All expected URI handlers should be registered"
    );

    for (size_t i = 0; i < URI_HANDLERS_COUNT; i++) {
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

    const int test_ports[] = {1, 80, 443, WEB_PORT, 65535};
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

    mock_setting_items_set_web_port(WEB_PORT);

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

// Тестируем симуляцию HTTP запроса GET / -> index_html_get_handler
void test_http_request_index_html(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP request simulation - GET / → index_html_get_handler");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(WEB_PORT);

    esp_err_t result = http_server_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "HTTP server should initialize successfully");

    bool request_handled = mock_simulate_http_request(HTTP_GET, "/");

    TEST_ASSERT_TRUE_MESSAGE(request_handled, "GET / request should be handled");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_httpd_resp_set_type_call_count,
                                 "httpd_resp_set_type should be called once");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("text/html", mock_last_content_type,
                                   "Content type should be set to text/html");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_httpd_resp_set_hdr_call_count,
                                 "httpd_resp_set_hdr should be called once");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Content-Encoding", mock_last_header_field,
                                   "Header field should be Content-Encoding");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("gzip", mock_last_header_value,
                                   "Header value should be gzip");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_httpd_resp_send_call_count,
                                 "httpd_resp_send should be called once");
}

// Тестируем симуляцию HTTP запроса GET /index.css -> index_css_get_handler
void test_http_request_index_css(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP request simulation - GET /index.css → index_css_get_handler");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(WEB_PORT);

    esp_err_t result = http_server_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "HTTP server should initialize successfully");

    bool request_handled = mock_simulate_http_request(HTTP_GET, "/index.css");

    TEST_ASSERT_TRUE_MESSAGE(request_handled, "GET /index.css request should be handled");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_httpd_resp_set_type_call_count,
                                 "httpd_resp_set_type should be called once");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("text/css", mock_last_content_type,
                                   "Content type should be set to text/css");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_httpd_resp_set_hdr_call_count,
                                 "httpd_resp_set_hdr should be called once");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Content-Encoding", mock_last_header_field,
                                   "Header field should be Content-Encoding");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("gzip", mock_last_header_value,
                                   "Header value should be gzip");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_httpd_resp_send_call_count,
                                 "httpd_resp_send should be called once");
}

// Тестируем симуляцию HTTP запроса GET /index.js -> index_js_get_handler
void test_http_request_index_js(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP request simulation - GET /index.js → index_js_get_handler");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(WEB_PORT);

    esp_err_t result = http_server_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "HTTP server should initialize successfully");

    bool request_handled = mock_simulate_http_request(HTTP_GET, "/index.js");

    TEST_ASSERT_TRUE_MESSAGE(request_handled, "GET /index.js request should be handled");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_httpd_resp_set_type_call_count,
                                 "httpd_resp_set_type should be called once");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("application/javascript", mock_last_content_type,
                                   "Content type should be set to application/javascript");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_httpd_resp_set_hdr_call_count,
                                 "httpd_resp_set_hdr should be called once");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Content-Encoding", mock_last_header_field,
                                   "Header field should be Content-Encoding");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("gzip", mock_last_header_value,
                                   "Header value should be gzip");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_httpd_resp_send_call_count,
                                 "httpd_resp_send should be called once");
}

// Тестируем симуляцию HTTP запроса GET /favicon.webp -> favicon_get_handler
void test_http_request_favicon(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP request simulation - GET /favicon.webp → favicon_get_handler");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(WEB_PORT);

    esp_err_t result = http_server_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "HTTP server should initialize successfully");

    bool request_handled = mock_simulate_http_request(HTTP_GET, "/favicon.webp");

    TEST_ASSERT_TRUE_MESSAGE(request_handled, "GET /favicon.webp request should be handled");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_httpd_resp_set_type_call_count,
                                 "httpd_resp_set_type should be called once");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("image/webp", mock_last_content_type,
                                   "Content type should be set to image/webp");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_httpd_resp_set_hdr_call_count,
                                 "httpd_resp_set_hdr should be called once");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Content-Encoding", mock_last_header_field,
                                   "Header field should be Content-Encoding");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("gzip", mock_last_header_value,
                                   "Header value should be gzip");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_httpd_resp_send_call_count,
                                 "httpd_resp_send should be called once");
}

// Тестируем неправильные HTTP методы для статических файлов
void test_http_request_invalid_method_index_html(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP request - invalid method POST / (should be GET)");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(WEB_PORT);

    esp_err_t result = http_server_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "HTTP server should initialize successfully");

    bool request_handled = mock_simulate_http_request(HTTP_POST, "/");

    TEST_ASSERT_FALSE_MESSAGE(request_handled, "POST / request should not be handled (GET only)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_httpd_resp_set_type_call_count,
                                 "httpd_resp_set_type should not be called for invalid method");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_httpd_resp_send_call_count,
                                 "httpd_resp_send should not be called for invalid method");
}

void test_http_request_invalid_method_index_css(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP request - invalid method POST /index.css (should be GET)");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(WEB_PORT);

    esp_err_t result = http_server_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "HTTP server should initialize successfully");

    bool request_handled = mock_simulate_http_request(HTTP_POST, "/index.css");

    TEST_ASSERT_FALSE_MESSAGE(request_handled, "POST /index.css request should not be handled (GET only)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_httpd_resp_set_type_call_count,
                                 "httpd_resp_set_type should not be called for invalid method");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_httpd_resp_send_call_count,
                                 "httpd_resp_send should not be called for invalid method");
}

void test_http_request_invalid_method_index_js(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP request - invalid method PUT /index.js (should be GET)");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(WEB_PORT);

    esp_err_t result = http_server_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "HTTP server should initialize successfully");

    bool request_handled = mock_simulate_http_request(HTTP_PUT, "/index.js");

    TEST_ASSERT_FALSE_MESSAGE(request_handled, "PUT /index.js request should not be handled (GET only)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_httpd_resp_set_type_call_count,
                                 "httpd_resp_set_type should not be called for invalid method");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_httpd_resp_send_call_count,
                                 "httpd_resp_send should not be called for invalid method");
}

void test_http_request_invalid_method_favicon(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP request - invalid method DELETE /favicon.webp (should be GET)");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(WEB_PORT);

    esp_err_t result = http_server_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "HTTP server should initialize successfully");

    bool request_handled = mock_simulate_http_request(HTTP_DELETE, "/favicon.webp");

    TEST_ASSERT_FALSE_MESSAGE(request_handled, "DELETE /favicon.webp request should not be handled (GET only)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_httpd_resp_set_type_call_count,
                                 "httpd_resp_set_type should not be called for invalid method");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_httpd_resp_send_call_count,
                                 "httpd_resp_send should not be called for invalid method");
}

// Тестируем неправильные URI для статических файлов
void test_http_request_malformed_uri_index_html(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP request - malformed URI /index.html (should be /)");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(WEB_PORT);

    esp_err_t result = http_server_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "HTTP server should initialize successfully");

    bool request_handled = mock_simulate_http_request(HTTP_GET, "/index.html");

    TEST_ASSERT_FALSE_MESSAGE(request_handled, "GET /index.html request should not be handled (/ only)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_httpd_resp_set_type_call_count,
                                 "httpd_resp_set_type should not be called for malformed URI");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_httpd_resp_send_call_count,
                                 "httpd_resp_send should not be called for malformed URI");
}

void test_http_request_malformed_uri_index_css(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP request - malformed URI /css/index.css (should be /index.css)");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(WEB_PORT);

    esp_err_t result = http_server_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "HTTP server should initialize successfully");

    bool request_handled = mock_simulate_http_request(HTTP_GET, "/css/index.css");

    TEST_ASSERT_FALSE_MESSAGE(request_handled, "GET /css/index.css request should not be handled (/index.css only)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_httpd_resp_set_type_call_count,
                                 "httpd_resp_set_type should not be called for malformed URI");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_httpd_resp_send_call_count,
                                 "httpd_resp_send should not be called for malformed URI");
}

void test_http_request_malformed_uri_index_js(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP request - malformed URI /js/index.js (should be /index.js)");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(WEB_PORT);

    esp_err_t result = http_server_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "HTTP server should initialize successfully");

    bool request_handled = mock_simulate_http_request(HTTP_GET, "/js/index.js");

    TEST_ASSERT_FALSE_MESSAGE(request_handled, "GET /js/index.js request should not be handled (/index.js only)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_httpd_resp_set_type_call_count,
                                 "httpd_resp_set_type should not be called for malformed URI");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_httpd_resp_send_call_count,
                                 "httpd_resp_send should not be called for malformed URI");
}

void test_http_request_malformed_uri_favicon(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP request - malformed URI /favicon.ico (should be /favicon.webp)");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(WEB_PORT);

    esp_err_t result = http_server_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "HTTP server should initialize successfully");

    bool request_handled = mock_simulate_http_request(HTTP_GET, "/favicon.ico");

    TEST_ASSERT_FALSE_MESSAGE(request_handled, "GET /favicon.ico request should not be handled (/favicon.webp only)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_httpd_resp_set_type_call_count,
                                 "httpd_resp_set_type should not be called for malformed URI");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_httpd_resp_send_call_count,
                                 "httpd_resp_send should not be called for malformed URI");
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
    RUN_TEST(test_http_request_index_html);
    RUN_TEST(test_http_request_index_css);
    RUN_TEST(test_http_request_index_js);
    RUN_TEST(test_http_request_favicon);
    RUN_TEST(test_http_request_invalid_method_index_html);
    RUN_TEST(test_http_request_invalid_method_index_css);
    RUN_TEST(test_http_request_invalid_method_index_js);
    RUN_TEST(test_http_request_invalid_method_favicon);
    RUN_TEST(test_http_request_malformed_uri_index_html);
    RUN_TEST(test_http_request_malformed_uri_index_css);
    RUN_TEST(test_http_request_malformed_uri_index_js);
    RUN_TEST(test_http_request_malformed_uri_favicon);

    return UNITY_END();
}
