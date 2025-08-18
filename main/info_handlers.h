#pragma once

#include <esp_http_server.h>

// HTTP handler for system info GET endpoint
esp_err_t info_get_handler(httpd_req_t *req);
// HTTP handler for system info POST endpoint
esp_err_t info_post_handler(httpd_req_t *req);
// HTTP handler for system uptime GET endpoint
esp_err_t uptime_get_handler(httpd_req_t *req);
// HTTP handler for AP clients GET endpoint
esp_err_t ap_clients_get_handler(httpd_req_t *req);
