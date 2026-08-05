#include "json_utils.h"
#include "cJSON.h"
#include <stdbool.h>
#include <string.h>

/* The login/pass pair the next POST /auth carries; NULL login = "no JSON in the request". */
static const char *req_login = NULL;
static const char *req_pass = NULL;
static bool req_has_login_key = false;
static bool req_has_pass_key = false;

static cJSON req_root;
static cJSON req_login_item;
static cJSON req_pass_item;
static cJSON resp_root;

/* What auth.c wrote into the response. */
int mock_json_response_auth = -1;        /* value of the "auth" flag, -1 = not written */
int mock_json_response_logout = -1;      /* value of the "logout" flag, -1 = not written */
const char *mock_json_response_error = NULL;
int mock_json_utils_send_response_called = 0;
int mock_json_utils_send_error_called = 0;

void mock_json_utils_reset(void)
{
    req_login = NULL;
    req_pass = NULL;
    req_has_login_key = false;
    req_has_pass_key = false;

    mock_json_response_auth = -1;
    mock_json_response_logout = -1;
    mock_json_response_error = NULL;
    mock_json_utils_send_response_called = 0;
    mock_json_utils_send_error_called = 0;
}

/* Queue a login request body. Passing NULL for a field keeps the key out of the JSON. */
void mock_json_utils_set_credentials(const char *login, const char *pass)
{
    req_login = login;
    req_pass = pass;
    req_has_login_key = (login != NULL);
    req_has_pass_key = (pass != NULL);

    req_login_item.valuestring = (char *)login;
    req_login_item.type = cJSON_String;
    req_pass_item.valuestring = (char *)pass;
    req_pass_item.type = cJSON_String;
}

cJSON *json_utils_receive_json(httpd_req_t *req)
{
    (void)req;
    if (!req_has_login_key && !req_has_pass_key) {
        return NULL;
    }
    return &req_root;
}

void json_utils_send_response(httpd_req_t *req, cJSON *request_json, cJSON *response_json)
{
    (void)req;
    (void)request_json;
    (void)response_json;
    mock_json_utils_send_response_called++;
}

esp_err_t json_utils_send_error(httpd_req_t *req, const char *error_message)
{
    (void)req;
    mock_json_utils_send_error_called++;
    mock_json_response_error = error_message;
    return ESP_OK;
}

void json_utils_cleanup(cJSON *request_json, cJSON *response_json)
{
    (void)request_json;
    (void)response_json;
}

cJSON *cJSON_CreateObject(void)
{
    return &resp_root;
}

int cJSON_HasObjectItem(cJSON *object, const char *name)
{
    (void)object;
    if (strcmp(name, "login") == 0) {
        return req_has_login_key;
    }
    if (strcmp(name, "pass") == 0) {
        return req_has_pass_key;
    }
    return 0;
}

cJSON *cJSON_GetObjectItem(cJSON *object, const char *name)
{
    (void)object;
    if (strcmp(name, "login") == 0) {
        return &req_login_item;
    }
    if (strcmp(name, "pass") == 0) {
        return &req_pass_item;
    }
    return NULL;
}

cJSON *cJSON_AddStringToObject(cJSON *object, const char *name, const char *value)
{
    (void)object;
    if (strcmp(name, "error") == 0) {
        mock_json_response_error = value;
    }
    return &resp_root;
}

cJSON *cJSON_AddBoolToObject(cJSON *object, const char *name, int boolean)
{
    (void)object;
    if (strcmp(name, "auth") == 0) {
        mock_json_response_auth = boolean ? 1 : 0;
    }
    if (strcmp(name, "logout") == 0) {
        mock_json_response_logout = boolean ? 1 : 0;
    }
    return &resp_root;
}

void cJSON_Delete(cJSON *item)
{
    (void)item;
}
