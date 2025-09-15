#include "wifi_scan.h"

esp_err_t wifi_scan_init(void)
{
    mock_wifi_scan_init_call_count++;
    return mock_wifi_scan_init_return_value;
}

esp_err_t wifi_scan_start_handler(httpd_req_t *req)
{
    mock_wifi_scan_start_handler_called = 1;
    return ESP_OK;
}

esp_err_t wifi_scan_results_handler(httpd_req_t *req)
{
    mock_wifi_scan_results_handler_called = 1;
    return ESP_OK;
}
