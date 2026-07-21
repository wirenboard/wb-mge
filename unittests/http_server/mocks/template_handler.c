#include "template_handler.h"

esp_err_t template_handler_init(void) { return ESP_OK; }

esp_err_t template_handler_load(char **buf, size_t *len)
{
    (void)buf;
    (void)len;
    return ESP_FAIL;
}

esp_err_t template_upload_post_handler(httpd_req_t *req) { (void)req; return ESP_OK; }

esp_err_t template_get_handler(httpd_req_t *req) { (void)req; return ESP_OK; }
