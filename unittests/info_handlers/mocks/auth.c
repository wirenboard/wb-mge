/* auth mock for the info_handlers unit test. info_build_ap_clients_json() does
 * not authenticate, but the translation unit's HTTP handlers reference these. */

#include "auth.h"

esp_err_t auth_init(void)                               { return ESP_OK; }
esp_err_t auth_login_handler(httpd_req_t *req)          { (void)req; return ESP_OK; }
esp_err_t auth_logout_handler(httpd_req_t *req)         { (void)req; return ESP_OK; }
esp_err_t auth_session_check_handler(httpd_req_t *req)  { (void)req; return ESP_OK; }
bool      auth_middleware_check(httpd_req_t *req)       { (void)req; return true; }
