#include "json_utils.h"
#include "cJSON.h"
#include <string.h>

/* Injected hex string for handler tests — NULL means "no injection" */
static const char *mock_hex_inject = NULL;
static cJSON mock_req_root = {0};
static cJSON mock_hex_item = {0};

/* Track json_utils_send_error calls for assertion */
int mock_json_utils_send_error_called;
const char *mock_json_utils_send_error_last_msg;

/* Track json_utils_send_response calls */
int mock_json_utils_send_response_called;

void mock_json_utils_inject_hex(const char *hex)
{
    mock_hex_inject = hex;
    mock_hex_item.valuestring = (char *)hex;
}

void mock_json_utils_reset(void)
{
    mock_hex_inject = NULL;
    mock_hex_item.valuestring = NULL;
    mock_json_utils_send_error_called = 0;
    mock_json_utils_send_error_last_msg = NULL;
    mock_json_utils_send_response_called = 0;
}

cJSON *json_utils_receive_json(httpd_req_t *req)
{
    (void)req;
    if (mock_hex_inject != NULL) return &mock_req_root;
    return NULL;
}

cJSON *cJSON_GetObjectItem(cJSON *o, const char *k)
{
    (void)o;
    if (mock_hex_inject != NULL && k && strcmp(k, "hex") == 0) {
        return &mock_hex_item;
    }
    return NULL;
}

void json_utils_send_response(httpd_req_t *req, cJSON *req_json, cJSON *resp_json)
{
    (void)req;
    (void)req_json;
    (void)resp_json;
    mock_json_utils_send_response_called++;
}

esp_err_t json_utils_send_error(httpd_req_t *req, const char *error_message)
{
    (void)req;
    mock_json_utils_send_error_called++;
    mock_json_utils_send_error_last_msg = error_message;
    return ESP_OK;
}

void json_utils_cleanup(cJSON *req_json, cJSON *resp_json)
{
    (void)req_json;
    (void)resp_json;
}
