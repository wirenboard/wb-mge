#include "cmd_handler.h"

esp_err_t cmd_post_handler(httpd_req_t *req)
{
    mock_cmd_post_handler_called = 1;
    return ESP_OK;
}
