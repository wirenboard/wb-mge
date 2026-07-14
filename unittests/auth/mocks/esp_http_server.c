#include "esp_http_server.h"
#include <string.h>

/* Cookie handed to auth.c on the next httpd_req_get_cookie_val() call; NULL = no cookie. */
static const char *mock_cookie_value = NULL;

int mock_httpd_resp_send_called = 0;
int mock_httpd_resp_set_hdr_called = 0;
const char *mock_httpd_resp_set_hdr_last_value = NULL;
const char *mock_httpd_resp_set_status_last = NULL;

void mock_esp_http_server_reset(void)
{
    mock_cookie_value = NULL;
    mock_httpd_resp_send_called = 0;
    mock_httpd_resp_set_hdr_called = 0;
    mock_httpd_resp_set_hdr_last_value = NULL;
    mock_httpd_resp_set_status_last = NULL;
}

void mock_esp_http_server_set_cookie(const char *value)
{
    mock_cookie_value = value;
}

esp_err_t httpd_req_get_cookie_val(httpd_req_t *req, const char *cookie_name,
                                   char *val, size_t *val_size)
{
    (void)req;
    (void)cookie_name;

    if (mock_cookie_value == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    size_t len = strlen(mock_cookie_value);
    if (len + 1 > *val_size) {
        return ESP_FAIL;    // the real API answers ESP_ERR_HTTPD_RESULT_TRUNC; auth.c only checks for != ESP_OK
    }
    memcpy(val, mock_cookie_value, len + 1);
    *val_size = len + 1;
    return ESP_OK;
}

esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *field, const char *value)
{
    (void)req;
    (void)field;
    mock_httpd_resp_set_hdr_called++;
    mock_httpd_resp_set_hdr_last_value = value;
    return ESP_OK;
}

esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *status)
{
    (void)req;
    mock_httpd_resp_set_status_last = status;
    return ESP_OK;
}

esp_err_t httpd_resp_send(httpd_req_t *req, const char *buf, ssize_t buf_len)
{
    (void)req;
    (void)buf;
    (void)buf_len;
    mock_httpd_resp_send_called++;
    return ESP_OK;
}
