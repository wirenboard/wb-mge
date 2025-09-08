#include "auth.h"

esp_err_t auth_init(void)
{
    mock_auth_init_call_count++;
    return mock_auth_init_return_value;
}

esp_err_t auth_login_handler(httpd_req_t *req)
{
    mock_auth_login_handler_called = 1;
    return ESP_OK;
}

esp_err_t auth_logout_handler(httpd_req_t *req)
{
    mock_auth_logout_handler_called = 1;
    return ESP_OK;
}

esp_err_t auth_session_check_handler(httpd_req_t *req)
{
    mock_auth_session_check_handler_called = 1;
    return ESP_OK;
}

bool auth_middleware_check(httpd_req_t *req)
{
    return true;
}
