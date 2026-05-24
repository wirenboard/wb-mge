#include "json_utils.h"

/* Minimal stubs — port_manager tests do not exercise HTTP handlers. */

cJSON *json_utils_receive_json(httpd_req_t *req)
{
    (void)req;
    return NULL;
}

void json_utils_send_response(httpd_req_t *req, cJSON *req_json, cJSON *resp_json)
{
    (void)req;
    (void)req_json;
    (void)resp_json;
}

esp_err_t json_utils_send_error(httpd_req_t *req, const char *error_message)
{
    (void)req;
    (void)error_message;
    return ESP_OK;
}

void json_utils_cleanup(cJSON *req_json, cJSON *resp_json)
{
    (void)req_json;
    (void)resp_json;
}
