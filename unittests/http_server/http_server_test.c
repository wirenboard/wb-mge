#include "unity.h"
#include "console_log.h"

#include "array_size.h"
#include "esp_http_server.h"
#include "http_server.h"
#include "setting_items.h"

#include <string.h>

#define WEB_PORT                            8080

extern const uint8_t favicon_start[] asm("_binary_favicon_webp_gz_start");
extern const uint8_t favicon_end[] asm("_binary_favicon_webp_gz_end");

extern const uint8_t index_css_start[] asm("_binary_index_css_gz_start");
extern const uint8_t index_css_end[] asm("_binary_index_css_gz_end");

extern const uint8_t index_js_start[] asm("_binary_index_js_gz_start");
extern const uint8_t index_js_end[] asm("_binary_index_js_gz_end");

extern const uint8_t index_html_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_gz_end");

typedef struct {
    char uri[HTTPD_MAX_URI_LEN];
    enum http_method method;
} expected_uri_registry_entry_t;

void mock_setting_items_set_web_port(int port);

void setUp(void)
{
    esp_http_server_init();
    mock_handlers_reset();
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

    const expected_uri_registry_entry_t expected_uri_registry[] = {
        {"/auth",              HTTP_POST},
        {"/session",           HTTP_GET},
        {"/logout",            HTTP_POST},
        {"/",                  HTTP_GET},
        {"/index.css",         HTTP_GET},
        {"/index.js",          HTTP_GET},
        {"/favicon.webp",      HTTP_GET},
        {"/update",            HTTP_POST},
        {"/info",              HTTP_GET},
        {"/info",              HTTP_POST},
        {"/settings",          HTTP_GET},
        {"/settings",          HTTP_POST},
        {"/cmd",               HTTP_POST},
        {"/wifi_scan/start",   HTTP_POST},
        {"/wifi_scan/results", HTTP_GET},
        {"/ap_clients",        HTTP_GET},
        {"/uptime",            HTTP_GET}
    };

    size_t expected_count = ARRAY_SIZE(expected_uri_registry);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        expected_count,
        mock_uri_registry_count,
        "All expected URI handlers should be registered"
    );

    for (size_t i = 0; i < expected_count; i++) {
        bool found = false;
        for (size_t j = 0; j < mock_uri_registry_count; j++) {
            if (strcmp(expected_uri_registry[i].uri, mock_uri_registry[j].uri) == 0) {
                if (expected_uri_registry[i].method == mock_uri_registry[j].method) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            char search_message[MESSAGE_BUFFER_SIZE];
            const char* method_name = get_method_as_string(expected_uri_registry[i].method);
            snprintf(search_message, sizeof(search_message),
                    "URI %s with method %s should be registered\n", expected_uri_registry[i].uri, method_name);
            TEST_FAIL_MESSAGE(search_message);
        }
    }
}

// Тестируем граничные значения портов при инициализации HTTP сервера
void test_http_server_port_edge_cases(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP server port edge cases");
    LOG_MESSAGE();

    const int test_ports[] = {1, 80, 443, WEB_PORT, 65535};
    const size_t num_ports = ARRAY_SIZE(test_ports);

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

    mock_simulate_http_request(HTTP_GET, "/");

    TEST_ASSERT_EQUAL_PTR_MESSAGE(mock_current_request, mock_last_set_type_req,
                                 "httpd_resp_set_type should be called with correct req parameter");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(mock_current_request, mock_last_set_hdr_req,
                                 "httpd_resp_set_hdr should be called with correct req parameter");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(mock_current_request, mock_last_send_req,
                                 "httpd_resp_send should be called with correct req parameter");

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
    TEST_ASSERT_EQUAL_PTR_MESSAGE((const char *)index_html_start, mock_last_send_buf,
                                 "httpd_resp_send should be called with index_html_start as buffer");
    TEST_ASSERT_EQUAL_INT_MESSAGE(index_html_end - index_html_start, mock_last_send_buf_len,
                                 "httpd_resp_send should be called with correct buffer length");
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

    mock_simulate_http_request(HTTP_GET, "/index.css");

    TEST_ASSERT_EQUAL_PTR_MESSAGE(mock_current_request, mock_last_set_type_req,
                                 "httpd_resp_set_type should be called with correct req parameter");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(mock_current_request, mock_last_set_hdr_req,
                                 "httpd_resp_set_hdr should be called with correct req parameter");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(mock_current_request, mock_last_send_req,
                                 "httpd_resp_send should be called with correct req parameter");

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
    TEST_ASSERT_EQUAL_PTR_MESSAGE((const char *)index_css_start, mock_last_send_buf,
                                 "httpd_resp_send should be called with index_css_start as buffer");
    TEST_ASSERT_EQUAL_INT_MESSAGE(index_css_end - index_css_start, mock_last_send_buf_len,
                                 "httpd_resp_send should be called with correct buffer length");
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

    mock_simulate_http_request(HTTP_GET, "/index.js");

    TEST_ASSERT_EQUAL_PTR_MESSAGE(mock_current_request, mock_last_set_type_req,
                                 "httpd_resp_set_type should be called with correct req parameter");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(mock_current_request, mock_last_set_hdr_req,
                                 "httpd_resp_set_hdr should be called with correct req parameter");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(mock_current_request, mock_last_send_req,
                                 "httpd_resp_send should be called with correct req parameter");

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
    TEST_ASSERT_EQUAL_PTR_MESSAGE((const char *)index_js_start, mock_last_send_buf,
                                 "httpd_resp_send should be called with index_js_start as buffer");
    TEST_ASSERT_EQUAL_INT_MESSAGE(index_js_end - index_js_start, mock_last_send_buf_len,
                                 "httpd_resp_send should be called with correct buffer length");
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

    mock_simulate_http_request(HTTP_GET, "/favicon.webp");

    TEST_ASSERT_EQUAL_PTR_MESSAGE(mock_current_request, mock_last_set_type_req,
                                 "httpd_resp_set_type should be called with correct req parameter");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(mock_current_request, mock_last_set_hdr_req,
                                 "httpd_resp_set_hdr should be called with correct req parameter");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(mock_current_request, mock_last_send_req,
                                 "httpd_resp_send should be called with correct req parameter");

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
    TEST_ASSERT_EQUAL_PTR_MESSAGE((const char *)favicon_start, mock_last_send_buf,
                                 "httpd_resp_send should be called with favicon_start as buffer");
    TEST_ASSERT_EQUAL_INT_MESSAGE(favicon_end - favicon_start, mock_last_send_buf_len,
                                 "httpd_resp_send should be called with correct buffer length");
}

// Тестируем симуляцию HTTP запросов для всех внешних обработчиков
void test_http_request_external_handlers(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test HTTP request simulation - all external handlers");
    LOG_MESSAGE();

    mock_setting_items_set_web_port(WEB_PORT);

    esp_err_t result = http_server_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "HTTP server should initialize successfully");

    // Test authentication handlers
    LOG_INFO("Testing authentication handlers...");
    mock_simulate_http_request(HTTP_POST, "/auth");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_auth_login_handler_called,
                                 "auth_login_handler should be called for POST /auth");

    mock_simulate_http_request(HTTP_GET, "/session");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_auth_session_check_handler_called,
                                 "auth_session_check_handler should be called for GET /session");

    mock_simulate_http_request(HTTP_POST, "/logout");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_auth_logout_handler_called,
                                 "auth_logout_handler should be called for POST /logout");

    // Test OTA handler
    LOG_INFO("Testing OTA handler...");
    mock_simulate_http_request(HTTP_POST, "/update");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_ota_update_post_handler_called,
                                 "ota_update_post_handler should be called for POST /update");

    // Test info handlers
    LOG_INFO("Testing info handlers...");
    mock_simulate_http_request(HTTP_GET, "/info");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_info_get_handler_called,
                                 "info_get_handler should be called for GET /info");

    mock_simulate_http_request(HTTP_POST, "/info");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_info_post_handler_called,
                                 "info_post_handler should be called for POST /info");

    // Test settings handlers
    LOG_INFO("Testing settings handlers...");
    mock_simulate_http_request(HTTP_GET, "/settings");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_settings_get_handler_called,
                                 "settings_get_handler should be called for GET /settings");

    mock_simulate_http_request(HTTP_POST, "/settings");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_settings_post_handler_called,
                                 "settings_post_handler should be called for POST /settings");

    // Test command handler
    LOG_INFO("Testing command handler...");
    mock_simulate_http_request(HTTP_POST, "/cmd");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_cmd_post_handler_called,
                                 "cmd_post_handler should be called for POST /cmd");

    // Test WiFi scan handlers
    LOG_INFO("Testing WiFi scan handlers...");
    mock_simulate_http_request(HTTP_POST, "/wifi_scan/start");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_wifi_scan_start_handler_called,
                                 "wifi_scan_start_handler should be called for POST /wifi_scan/start");

    mock_simulate_http_request(HTTP_GET, "/wifi_scan/results");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_wifi_scan_results_handler_called,
                                 "wifi_scan_results_handler should be called for GET /wifi_scan/results");

    // Test utility handlers
    LOG_INFO("Testing utility handlers...");
    mock_simulate_http_request(HTTP_GET, "/ap_clients");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_ap_clients_get_handler_called,
                                 "ap_clients_get_handler should be called for GET /ap_clients");

    mock_simulate_http_request(HTTP_GET, "/uptime");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_uptime_get_handler_called,
                                 "uptime_get_handler should be called for GET /uptime");

    LOG_INFO("All external handlers tested successfully");
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
    RUN_TEST(test_http_request_external_handlers);

    return UNITY_END();
}
