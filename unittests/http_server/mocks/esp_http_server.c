#include "console_log.h"
#include "unity.h"
#include "esp_http_server.h"

#include <string.h>

int mock_httpd_start_call_count = 0;
int mock_httpd_register_uri_handler_call_count = 0;
int mock_wifi_scan_init_call_count = 0;
int mock_auth_init_call_count = 0;

esp_err_t mock_httpd_start_return_value = ESP_OK;
esp_err_t mock_wifi_scan_init_return_value = ESP_OK;
esp_err_t mock_auth_init_return_value = ESP_OK;

httpd_config_t mock_captured_config = {0};
char mock_registered_uris[MAX_URI_HANDLERS][HTTPD_MAX_URI_LEN] = {0};
httpd_handle_t mock_server_handle = (httpd_handle_t)0x12345678;

int mock_httpd_resp_set_type_call_count = 0;
int mock_httpd_resp_set_hdr_call_count = 0;
int mock_httpd_resp_send_call_count = 0;
char mock_last_content_type[128] = {0};
char mock_last_header_field[128] = {0};
char mock_last_header_value[128] = {0};

mock_uri_registry_entry_t mock_uri_registry[MAX_URI_HANDLERS];
int mock_uri_registry_count = 0;

void esp_http_server_init(void)
{
    mock_httpd_start_call_count = 0;
    mock_httpd_register_uri_handler_call_count = 0;
    mock_wifi_scan_init_call_count = 0;
    mock_auth_init_call_count = 0;

    mock_httpd_start_return_value = ESP_OK;
    mock_wifi_scan_init_return_value = ESP_OK;
    mock_auth_init_return_value = ESP_OK;

    memset(&mock_captured_config, 0, sizeof(mock_captured_config));
    memset(mock_registered_uris, 0, sizeof(mock_registered_uris));

    mock_httpd_resp_set_type_call_count = 0;
    mock_httpd_resp_set_hdr_call_count = 0;
    mock_httpd_resp_send_call_count = 0;
    memset(mock_last_content_type, 0, sizeof(mock_last_content_type));
    memset(mock_last_header_field, 0, sizeof(mock_last_header_field));
    memset(mock_last_header_value, 0, sizeof(mock_last_header_value));

    memset(mock_uri_registry, 0, sizeof(mock_uri_registry));
    mock_uri_registry_count = 0;
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

esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri_handler)
{
    if (mock_httpd_register_uri_handler_call_count < MAX_URI_HANDLERS && uri_handler && uri_handler->uri) {
        strncpy(mock_registered_uris[mock_httpd_register_uri_handler_call_count],
                uri_handler->uri,
                sizeof(mock_registered_uris[0]) - 1);
    }

    if (mock_uri_registry_count < MAX_URI_HANDLERS && uri_handler) {
        mock_uri_registry_entry_t *entry = &mock_uri_registry[mock_uri_registry_count];
        strncpy(entry->uri, uri_handler->uri, sizeof(entry->uri) - 1);
        entry->method = uri_handler->method;
        entry->handler = uri_handler->handler;
        entry->user_ctx = uri_handler->user_ctx;
        entry->registered = true;
        mock_uri_registry_count++;

        LOG_INFO("Registered handler");
    }

    mock_httpd_register_uri_handler_call_count++;
    return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type)
{
    mock_httpd_resp_set_type_call_count++;
    if (type) {
        strncpy(mock_last_content_type, type, sizeof(mock_last_content_type) - 1);
    }
    return ESP_OK;
}

esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *field, const char *value)
{
    mock_httpd_resp_set_hdr_call_count++;
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

    return ESP_OK;
}

httpd_req_t mock_create_request(const char* uri, enum http_method method)
{
    httpd_req_t req = {0};
    req.handle = mock_server_handle;
    req.method = method;
    strncpy((char*)req.uri, uri, HTTPD_MAX_URI_LEN);
    req.content_len = 0;
    req.aux = NULL;
    req.user_ctx = NULL;
    return req;
}

bool mock_simulate_http_request(enum http_method method, const char* uri)
{
    TEST_ASSERT_LESS_THAN_INT_MESSAGE(HTTP_ANY, method, "Invalid method");
    TEST_ASSERT_NOT_NULL_MESSAGE(uri, "Invalid URI");

    for (int i = 0; i < mock_uri_registry_count; i++) {
        mock_uri_registry_entry_t *entry = &mock_uri_registry[i];

        if (entry->registered &&
            entry->method == method &&
            strcmp(entry->uri, uri) == 0) {

            httpd_req_t mock_req = mock_create_request(uri, method);
            mock_req.user_ctx = entry->user_ctx;

            LOG_INFO("Found handler, executing...");

            esp_err_t result = entry->handler(&mock_req);

            LOG_INFO("Handler execution completed...");

            if (result == ESP_OK) {
                LOG_INFO("✓ HTTP request simulation successful");
            } else {
                LOG_INFO("❌ Handler returned error");
            }
            return true;
        }
    }

    LOG_INFO("❌ No handler found");
    return false;
}
