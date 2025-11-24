#include "info_handlers.h"

esp_err_t info_get_handler(httpd_req_t *req)
{
    mock_info_get_handler_called = 1;
    return ESP_OK;
}

esp_err_t ap_clients_get_handler(httpd_req_t *req)
{
    mock_ap_clients_get_handler_called = 1;
    return ESP_OK;
}

esp_err_t uptime_get_handler(httpd_req_t *req)
{
    mock_uptime_get_handler_called = 1;
    return ESP_OK;
}

esp_err_t wb_status_get_handler(httpd_req_t *req)
{
    mock_wb_status_get_handler_called = 1;
    return ESP_OK;
}
