#ifndef AUTH_HANDLERS_H
#define AUTH_HANDLERS_H

#include <esp_http_server.h>
#include "cJSON.h"

// Authentication constants
#define AUTH_COOKIE_MAX_LEN          64
#define AUTH_REMEMBER_COOKIE_MAX_LEN 256

/**
 * @brief Initialize authentication handlers module
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t auth_handlers_init(void);

/**
 * @brief HTTP handler for authentication/login endpoint
 * @param req HTTP request handle
 * @return ESP_OK on success
 */
esp_err_t auth_login_handler(httpd_req_t *req);

/**
 * @brief HTTP handler for logout endpoint
 * @param req HTTP request handle
 * @return ESP_OK on success
 */
esp_err_t auth_logout_handler(httpd_req_t *req);

/**
 * @brief HTTP handler for session check endpoint
 * @param req HTTP request handle
 * @return ESP_OK on success
 */
esp_err_t auth_session_check_handler(httpd_req_t *req);

/**
 * @brief Authentication middleware check
 * @param req HTTP request handle
 * @return true if authenticated, false otherwise
 */
bool auth_middleware_check(httpd_req_t *req);

/**
 * @brief Set session cookie in HTTP response
 * @param req HTTP request handle
 * @param session_id Session ID to set
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t auth_set_session_cookie(httpd_req_t *req, uint32_t session_id);

/**
 * @brief Set remember token cookie in HTTP response
 * @param req HTTP request handle
 * @param token Remember token string
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t auth_set_remember_cookie(httpd_req_t *req, const char *token);

/**
 * @brief Clear authentication cookies
 * @param req HTTP request handle
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t auth_clear_auth_cookies(httpd_req_t *req);

/**
 * @brief Validate login credentials from JSON
 * @param request_json JSON object containing login credentials
 * @param response_json JSON object to store validation result
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t auth_validate_credentials(cJSON *request_json, cJSON *response_json);

/**
 * @brief Process remember me functionality
 * @param req HTTP request handle
 * @param remember_me_item Remember me JSON item
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t auth_process_remember_me(httpd_req_t *req, cJSON *remember_me_item);

#endif // AUTH_HANDLERS_H