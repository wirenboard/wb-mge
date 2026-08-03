#include "json_utils.h"
#include "cJSON.h"
#include <stdbool.h>
#include <string.h>

/* Injected JSON fields for handler tests — NULL means "no injection".
 * A request is considered present if any field was injected. */
static const char *mock_hex_inject = NULL;
static const char *mock_mode_inject = NULL;
/* "enabled" is a bool, so it has no NULL to mean "not injected" — a separate flag
 * carries that, and the value itself lives in the item's type bits. */
static bool mock_enabled_injected = false;
static cJSON mock_req_root = {0};
static cJSON mock_hex_item = {0};
static cJSON mock_mode_item = {0};
static cJSON mock_enabled_item = {0};

/* Track json_utils_send_error calls for assertion */
int mock_json_utils_send_error_called;
const char *mock_json_utils_send_error_last_msg;
const char *mock_json_utils_send_error_last_status;

/* Track json_utils_send_response calls */
int mock_json_utils_send_response_called;

void mock_json_utils_inject_hex(const char *hex)
{
    mock_hex_inject = hex;
    mock_hex_item.valuestring = (char *)hex;
}

void mock_json_utils_inject_mode(const char *mode)
{
    mock_mode_inject = mode;
    mock_mode_item.valuestring = (char *)mode;
}

void mock_json_utils_inject_enabled(bool enabled)
{
    mock_enabled_injected = true;
    mock_enabled_item.type = enabled ? cJSON_True : cJSON_False;
}

void mock_json_utils_reset(void)
{
    mock_hex_inject = NULL;
    mock_hex_item.valuestring = NULL;
    mock_mode_inject = NULL;
    mock_mode_item.valuestring = NULL;
    mock_enabled_injected = false;
    mock_enabled_item.type = 0;
    mock_json_utils_send_error_called = 0;
    mock_json_utils_send_error_last_msg = NULL;
    mock_json_utils_send_error_last_status = NULL;
    mock_json_utils_send_response_called = 0;
}

cJSON *json_utils_receive_json(httpd_req_t *req)
{
    (void)req;
    if (mock_hex_inject != NULL || mock_mode_inject != NULL || mock_enabled_injected) {
        return &mock_req_root;
    }
    return NULL;
}

cJSON *cJSON_GetObjectItem(cJSON *o, const char *k)
{
    (void)o;
    if (mock_hex_inject != NULL && k && strcmp(k, "hex") == 0) {
        return &mock_hex_item;
    }
    if (mock_mode_inject != NULL && k && strcmp(k, "mode") == 0) {
        return &mock_mode_item;
    }
    if (mock_enabled_injected && k && strcmp(k, "enabled") == 0) {
        return &mock_enabled_item;
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

esp_err_t json_utils_send_error_status(httpd_req_t *req, const char *status,
                                       const char *error_message)
{
    (void)req;
    mock_json_utils_send_error_called++;
    mock_json_utils_send_error_last_status = status;
    mock_json_utils_send_error_last_msg = error_message;
    return ESP_OK;
}

esp_err_t json_utils_send_error(httpd_req_t *req, const char *error_message)
{
    return json_utils_send_error_status(req, "400 Bad Request", error_message);
}

void json_utils_cleanup(cJSON *req_json, cJSON *resp_json)
{
    (void)req_json;
    (void)resp_json;
}
