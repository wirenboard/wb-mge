#include <esp_http_server.h>

esp_err_t wb_test_get_handler(httpd_req_t *req)
{
    mock_wb_test_get_handler_called = 1;
    return ESP_OK;
}

esp_err_t wb_test_post_handler(httpd_req_t *req)
{
    mock_wb_test_post_handler_called = 1;
    return ESP_OK;
}
