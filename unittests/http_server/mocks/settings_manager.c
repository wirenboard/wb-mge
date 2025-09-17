#include "settings_manager.h"

esp_err_t settings_get_handler(httpd_req_t *req)
{
    mock_settings_get_handler_called = 1;
    return ESP_OK;
}

esp_err_t settings_post_handler(httpd_req_t *req)
{
    mock_settings_post_handler_called = 1;
    return ESP_OK;
}
