#pragma once

#include <esp_http_server.h>
#include "setting_items.h"

// HTTP handler for settings GET endpoint
esp_err_t settings_get_handler(httpd_req_t *req);
// HTTP handler for settings POST endpoint
esp_err_t settings_post_handler(httpd_req_t *req);
