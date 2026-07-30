/* auth mock for the info_handlers unit test. info_build_ap_clients_json() does
 * not authenticate, and most of these symbols exist only because the translation
 * unit's HTTP handlers reference them. auth_middleware_check() is the exception:
 * it is on the executed path, and its unconditional true is load-bearing. Returning
 * false would make info_get_handler() return early at info_handlers.c:269-271
 * without building a response, and the GET /info tests would fail on the
 * TEST_ASSERT_NOT_NULL for the emitted object. */

#include "auth.h"

esp_err_t auth_init(void)                               { return ESP_OK; }
esp_err_t auth_login_handler(httpd_req_t *req)          { (void)req; return ESP_OK; }
esp_err_t auth_logout_handler(httpd_req_t *req)         { (void)req; return ESP_OK; }
esp_err_t auth_session_check_handler(httpd_req_t *req)  { (void)req; return ESP_OK; }
bool      auth_middleware_check(httpd_req_t *req)       { (void)req; return true; }
