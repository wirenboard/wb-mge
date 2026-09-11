/* Stub for auth middleware — every command test runs as an authenticated request. */

#include "auth.h"

bool auth_middleware_check(httpd_req_t *req)
{
    (void)req;
    return true;
}

esp_err_t auth_init(void)
{
    return ESP_OK;
}

esp_err_t auth_login_handler(httpd_req_t *req)
{
    (void)req;
    return ESP_OK;
}

esp_err_t auth_logout_handler(httpd_req_t *req)
{
    (void)req;
    return ESP_OK;
}

esp_err_t auth_session_check_handler(httpd_req_t *req)
{
    (void)req;
    return ESP_OK;
}
