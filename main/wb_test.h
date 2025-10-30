#pragma once

#include "esp_err.h"
#include <esp_http_server.h>

// This module implements API handlers for internal production test purposes

esp_err_t wb_test_get_handler(httpd_req_t *req);
esp_err_t wb_test_post_handler(httpd_req_t *req);
