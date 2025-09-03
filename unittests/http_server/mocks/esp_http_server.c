#include "esp_http_server.h"
#include <string.h>
#include <stdio.h>

int mock_httpd_start_call_count = 0;
int mock_httpd_register_uri_handler_call_count = 0;
int mock_wifi_scan_init_call_count = 0;
int mock_auth_init_call_count = 0;

esp_err_t mock_httpd_start_return_value = ESP_OK;
esp_err_t mock_wifi_scan_init_return_value = ESP_OK;
esp_err_t mock_auth_init_return_value = ESP_OK;

httpd_config_t mock_captured_config = {0};
char mock_registered_uris[20][64] = {0};
httpd_handle_t mock_server_handle = (httpd_handle_t)0x12345678;

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
    if (mock_httpd_register_uri_handler_call_count < 20 && uri_handler && uri_handler->uri) {
        strncpy(mock_registered_uris[mock_httpd_register_uri_handler_call_count],
                uri_handler->uri,
                sizeof(mock_registered_uris[0]) - 1);
    }

    mock_httpd_register_uri_handler_call_count++;
    return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type)
{
    return ESP_OK;
}

esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *field, const char *value)
{
    return ESP_OK;
}

esp_err_t httpd_resp_send(httpd_req_t *req, const char *buf, ssize_t buf_len)
{
    return ESP_OK;
}
