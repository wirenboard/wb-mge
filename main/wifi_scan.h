#pragma once

#include <esp_http_server.h>

esp_err_t wifi_scan_init(void);

// HTTP handler for WiFi scan start endpoint
esp_err_t wifi_scan_start_handler(httpd_req_t *req);
// HTTP handler for WiFi scan results endpoint
esp_err_t wifi_scan_results_handler(httpd_req_t *req);
