#include "ota_handler.h"

esp_err_t ota_update_post_handler(httpd_req_t *req)
{
    mock_ota_update_post_handler_called = 1;
    return ESP_OK;
}
