#pragma once

#include <esp_http_server.h>
#include <esp_err.h>
#include <stdbool.h>

esp_err_t auth_init(void);

// HTTP handler for authentication/login endpoint
esp_err_t auth_login_handler(httpd_req_t *req);

// HTTP handler for logout endpoint
esp_err_t auth_logout_handler(httpd_req_t *req);

// HTTP handler for session check endpoint
esp_err_t auth_session_check_handler(httpd_req_t *req);

// Authentication middleware check. Returns true if the request is authenticated.
bool auth_middleware_check(httpd_req_t *req);
