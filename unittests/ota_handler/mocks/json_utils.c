/* json_utils mock for the ota_handler unit tests. Every response the handler sends goes through
 * here, and the error paths deliberately call the same httpd_resp_set_status() the auth mock uses,
 * so a test can tell an authentication answer from a 409 by looking at one place. */

#include "json_utils.h"
#include "esp_http_server.h"
#include "ota_mocks.h"

int         mock_json_utils_send_response_called = 0;
int         mock_json_utils_send_error_called = 0;
const char *mock_json_utils_last_error = NULL;
const char *mock_json_utils_last_error_status = NULL;

void mock_json_utils_reset(void)
{
    mock_json_utils_send_response_called = 0;
    mock_json_utils_send_error_called = 0;
    mock_json_utils_last_error = NULL;
    mock_json_utils_last_error_status = NULL;
}

cJSON *json_utils_receive_json(httpd_req_t *req)
{
    (void)req;
    return NULL;   // Not on any path ota_handler.c takes
}

void json_utils_send_response(httpd_req_t *req, cJSON *req_json, cJSON *resp_json)
{
    (void)req_json;
    (void)resp_json;
    mock_json_utils_send_response_called++;
    httpd_resp_send(req, NULL, 0);
}

esp_err_t json_utils_send_error_status(httpd_req_t *req, const char *status, const char *error_message)
{
    mock_json_utils_send_error_called++;
    mock_json_utils_last_error = error_message;
    mock_json_utils_last_error_status = status;
    httpd_resp_set_status(req, status);
    httpd_resp_send(req, NULL, 0);
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
