#include "http_server.h"

int mock_http_server_init_called = 0;
esp_err_t mock_http_server_init_return_value = ESP_OK;

int mock_http_server_deinit_called = 0;
esp_err_t mock_http_server_deinit_return_value = ESP_OK;

int mock_http_server_check_settings_changed_called = 0;
bool mock_http_server_check_settings_changed_return_value = false;

esp_err_t http_server_init(void)
{
    mock_http_server_init_called++;
    return mock_http_server_init_return_value;
}

esp_err_t http_server_deinit(void)
{
    mock_http_server_deinit_called++;
    return mock_http_server_deinit_return_value;
}

bool http_server_check_settings_changed(void)
{
    mock_http_server_check_settings_changed_called++;
    return mock_http_server_check_settings_changed_return_value;
}

void mock_http_server_reset(void)
{
    mock_http_server_init_called = 0;
    mock_http_server_init_return_value = ESP_OK;

    mock_http_server_deinit_called = 0;
    mock_http_server_deinit_return_value = ESP_OK;

    mock_http_server_check_settings_changed_called = 0;
    mock_http_server_check_settings_changed_return_value = false;
}
