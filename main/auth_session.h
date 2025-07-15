#ifndef AUTH_SESSION_H
#define AUTH_SESSION_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_http_server.h"
#include "esp_err.h"

// Constants
#define MAX_SESSIONS 10
#define REMEMBER_TOKEN_MAX_LEN 64
#define REMEMBER_TOKEN_LIFETIME_SEC (30 * 24 * 60 * 60) // 30 days
#define REMEMBER_COOKIE_BUF_SIZE (REMEMBER_TOKEN_MAX_LEN + 100)

// Session management
esp_err_t auth_session_init(void);
uint32_t auth_create_session(const char *login, const char *password);
bool auth_is_session_valid(uint32_t session_id);
void auth_remove_session(uint32_t session_id);
uint32_t auth_get_session_from_cookie(httpd_req_t *req);

// Remember token management
uint32_t auth_create_remember_token(void);
uint32_t auth_validate_remember_token(const char *token);
char* auth_get_remember_token_from_cookie(httpd_req_t *req);
const char* auth_get_current_remember_token(void);
void auth_clear_remember_token(void);

// Authentication check
bool auth_check_request(httpd_req_t *req);

#endif // AUTH_SESSION_H
