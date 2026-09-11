/* json_utils mock for the cmd_handler unit test.
 *
 * POST /cmd reads its command out of the request JSON and answers through this module, so the mock
 * does both halves: a test queues the request body with mock_json_utils_set_request(), and the
 * response the handler emitted is kept for inspection instead of being freed, exactly as the
 * info_handlers mock does. Ownership of the response passes to the caller of
 * mock_json_utils_take_response(); mock_json_utils_reset() frees one no test took. */

#include "json_utils.h"

#include <string.h>

static cJSON *mock_request = NULL;
static cJSON *mock_last_response = NULL;

int         mock_json_utils_send_error_called = 0;
const char *mock_json_utils_last_error = NULL;

/* Queue the body of the next POST. The mock takes ownership: the handler frees it through
 * json_utils_send_response() / json_utils_cleanup(), just as it does in production. */
void mock_json_utils_set_request(cJSON *request)
{
    if (mock_request != NULL) {
        cJSON_Delete(mock_request);
    }
    mock_request = request;
}

/* Hand the captured response to the caller, who becomes responsible for deleting it. */
cJSON *mock_json_utils_take_response(void)
{
    cJSON *resp = mock_last_response;
    mock_last_response = NULL;
    return resp;
}

void mock_json_utils_reset(void)
{
    if (mock_request != NULL) {
        cJSON_Delete(mock_request);
        mock_request = NULL;
    }
    if (mock_last_response != NULL) {
        cJSON_Delete(mock_last_response);
        mock_last_response = NULL;
    }
    mock_json_utils_send_error_called = 0;
    mock_json_utils_last_error = NULL;
}

cJSON *json_utils_receive_json(httpd_req_t *req)
{
    (void)req;
    cJSON *request = mock_request;
    mock_request = NULL;   /* handed to the handler, which frees it */
    return request;
}

void json_utils_send_response(httpd_req_t *req, cJSON *req_json, cJSON *resp_json)
{
    (void)req;
    if (req_json != NULL) {
        cJSON_Delete(req_json);
    }
    /* Only the most recent response is kept; drop an untaken earlier one. */
    if (mock_last_response != NULL) {
        cJSON_Delete(mock_last_response);
    }
    mock_last_response = resp_json;
}

esp_err_t json_utils_send_error(httpd_req_t *req, const char *error_message)
{
    (void)req;
    mock_json_utils_send_error_called++;
    mock_json_utils_last_error = error_message;
    return ESP_OK;
}

void json_utils_cleanup(cJSON *req_json, cJSON *resp_json)
{
    if (req_json != NULL) {
        cJSON_Delete(req_json);
    }
    if (resp_json != NULL) {
        cJSON_Delete(resp_json);
    }
}
