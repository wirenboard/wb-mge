#pragma once

#include <esp_http_server.h>

// HTTP handler for OTA update POST endpoint
esp_err_t ota_update_post_handler(httpd_req_t *req);
