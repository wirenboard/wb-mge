#include "console_log.h"
#include "unity.h"
#include "esp_http_server.h"

#include <string.h>

int mock_httpd_start_call_count = 0;
int mock_wifi_scan_init_call_count = 0;
int mock_auth_init_call_count = 0;

esp_err_t mock_httpd_start_return_value = ESP_OK;
esp_err_t mock_wifi_scan_init_return_value = ESP_OK;
esp_err_t mock_auth_init_return_value = ESP_OK;

httpd_config_t mock_captured_config = {0};
httpd_handle_t mock_server_handle = (httpd_handle_t)0x12345678;

int mock_auth_login_handler_called = 0;
int mock_auth_session_check_handler_called = 0;
int mock_auth_logout_handler_called = 0;
int mock_ota_update_post_handler_called = 0;
int mock_info_get_handler_called = 0;
int mock_settings_get_handler_called = 0;
int mock_settings_post_handler_called = 0;
int mock_cmd_post_handler_called = 0;
int mock_wifi_scan_start_handler_called = 0;
int mock_wifi_scan_results_handler_called = 0;
int mock_ap_clients_get_handler_called = 0;
int mock_uptime_get_handler_called = 0;

int mock_httpd_resp_set_type_call_count = 0;
int mock_httpd_resp_set_hdr_call_count = 0;
int mock_httpd_resp_send_call_count = 0;
char mock_last_content_type[128] = {0};
char mock_last_header_field[128] = {0};
char mock_last_header_value[128] = {0};
const char* mock_last_send_buf = NULL;
ssize_t mock_last_send_buf_len = 0;
httpd_req_t* mock_last_set_type_req = NULL;
httpd_req_t* mock_last_set_hdr_req = NULL;
httpd_req_t* mock_last_send_req = NULL;
httpd_req_t* mock_current_request = NULL;

static httpd_req_t mock_request_object;

mock_uri_registry_entry_t mock_uri_registry[MAX_URI_HANDLERS];
int mock_uri_registry_count = 0;

void esp_http_server_init(void)
{
    mock_httpd_start_call_count = 0;
    mock_wifi_scan_init_call_count = 0;
    mock_auth_init_call_count = 0;

    mock_httpd_start_return_value = ESP_OK;
    mock_wifi_scan_init_return_value = ESP_OK;
    mock_auth_init_return_value = ESP_OK;

    memset(&mock_captured_config, 0, sizeof(mock_captured_config));

    mock_httpd_resp_set_type_call_count = 0;
    mock_httpd_resp_set_hdr_call_count = 0;
    mock_httpd_resp_send_call_count = 0;
    memset(mock_last_content_type, 0, sizeof(mock_last_content_type));
    memset(mock_last_header_field, 0, sizeof(mock_last_header_field));
    memset(mock_last_header_value, 0, sizeof(mock_last_header_value));
    mock_last_send_buf = NULL;
    mock_last_send_buf_len = 0;
    mock_last_set_type_req = NULL;
    mock_last_set_hdr_req = NULL;
    mock_last_send_req = NULL;
    mock_current_request = NULL;

    memset(mock_uri_registry, 0, sizeof(mock_uri_registry));
    mock_uri_registry_count = 0;
}

void mock_handlers_reset(void)
{
    mock_auth_login_handler_called = 0;
    mock_auth_session_check_handler_called = 0;
    mock_auth_logout_handler_called = 0;
    mock_ota_update_post_handler_called = 0;
    mock_info_get_handler_called = 0;
    mock_settings_get_handler_called = 0;
    mock_settings_post_handler_called = 0;
    mock_cmd_post_handler_called = 0;
    mock_wifi_scan_start_handler_called = 0;
    mock_wifi_scan_results_handler_called = 0;
    mock_ap_clients_get_handler_called = 0;
    mock_uptime_get_handler_called = 0;
}

esp_err_t httpd_start(httpd_handle_t *handle, const httpd_config_t *config)
{
    mock_httpd_start_call_count++;

    if (config) {
        mock_captured_config = *config;
    }

    if (mock_httpd_start_return_value == ESP_OK) {
        *handle = mock_server_handle;
    } else {
        *handle = NULL;
    }

    return mock_httpd_start_return_value;
}

esp_err_t httpd_stop(httpd_handle_t handle)
{
    return ESP_OK;
}

esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri_handler)
{
    if (uri_handler) {
        if (mock_uri_registry_count < MAX_URI_HANDLERS) {
            mock_uri_registry_entry_t *entry = &mock_uri_registry[mock_uri_registry_count];
            strncpy(entry->uri, uri_handler->uri, sizeof(entry->uri) - 1);
            entry->uri[sizeof(entry->uri) - 1] = '\0';
            entry->method = uri_handler->method;
            entry->handler = uri_handler->handler;
            entry->user_ctx = uri_handler->user_ctx;
            entry->registered = true;

            char message[MESSAGE_BUFFER_SIZE];
            const char* method_name = get_method_as_string(entry->method);
            snprintf(message, sizeof(message), "Registered URI handler %s with method %s", entry->uri, method_name);
            LOG_INFO("%s", message);

            mock_uri_registry_count++;
        }
    }
    return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type)
{
    mock_httpd_resp_set_type_call_count++;
    mock_last_set_type_req = req;
    if (type) {
        strncpy(mock_last_content_type, type, sizeof(mock_last_content_type) - 1);
    }
    return ESP_OK;
}

esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *field, const char *value)
{
    mock_httpd_resp_set_hdr_call_count++;
    mock_last_set_hdr_req = req;
    if (field) {
        strncpy(mock_last_header_field, field, sizeof(mock_last_header_field) - 1);
    }
    if (value) {
        strncpy(mock_last_header_value, value, sizeof(mock_last_header_value) - 1);
    }
    return ESP_OK;
}

esp_err_t httpd_resp_send(httpd_req_t *req, const char *buf, ssize_t buf_len)
{
    mock_httpd_resp_send_call_count++;
    mock_last_send_req = req;
    mock_last_send_buf = buf;
    mock_last_send_buf_len = buf_len;

    return ESP_OK;
}

static httpd_req_t mock_create_request(const char* uri, enum http_method method)
{
    httpd_req_t req = {0};
    req.handle = mock_server_handle;
    req.method = method;
    strncpy(req.uri, uri, sizeof(req.uri) - 1);
    req.uri[sizeof(req.uri) - 1] = '\0';
    req.content_len = 0;
    req.aux = NULL;
    req.user_ctx = NULL;
    return req;
}

esp_err_t mock_simulate_http_request(enum http_method method, const char* uri)
{
    char method_message[MESSAGE_BUFFER_SIZE];
    const char* method_name = get_method_as_string(method);
    snprintf(method_message, sizeof(method_message), "Invalid method: %s", method_name);
    TEST_ASSERT_LESS_THAN_INT_MESSAGE(HTTP_ANY, method, method_message);

    char uri_message[MESSAGE_BUFFER_SIZE];
    snprintf(uri_message, sizeof(uri_message), "Invalid URI: %s", uri);
    TEST_ASSERT_NOT_NULL_MESSAGE(uri, uri_message);

    for (int i = 0; i < mock_uri_registry_count; i++) {
        mock_uri_registry_entry_t *entry = &mock_uri_registry[i];

        if (entry->registered) {
            if (entry->method == method) {
                if (strcmp(entry->uri, uri) == 0) {

                    mock_request_object = mock_create_request(uri, method);
                    mock_request_object.user_ctx = entry->user_ctx;
                    mock_current_request = &mock_request_object;

                    esp_err_t result = entry->handler(&mock_request_object);
                    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Handler returned error");

                    return ESP_OK;
                }
            }
        }
    }

    char handler_message[MESSAGE_BUFFER_SIZE];
    snprintf(handler_message, sizeof(handler_message), "No handler found for URI %s with method %s", uri, method_name);
    TEST_FAIL_MESSAGE(handler_message);
}
