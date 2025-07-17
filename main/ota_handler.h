#pragma once

#include <esp_http_server.h>

esp_err_t ota_handler_init(void);

// HTTP handler for OTA update POST endpoint
esp_err_t ota_update_post_handler(httpd_req_t *req);
