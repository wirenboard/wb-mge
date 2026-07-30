/* json_utils mock for the info_handlers unit test. The HTTP handlers in the
 * translation unit call send_response/send_error. Instead of freeing the response
 * JSON the way the real API does, send_response keeps it so a test can inspect what
 * a handler actually emitted; ownership is handed over by
 * mock_json_utils_take_response(), and mock_json_utils_reset() frees a response no test
 * took. It cannot cover a response that WAS taken: take_response() clears the mock's
 * pointer, so if an assertion fails before the test's own cJSON_Delete(), Unity longjmps
 * out and tearDown has nothing left to free. That leaks one object per failing test —
 * bounded, and only on a run that is already reporting a failure. */

#include "json_utils.h"

static cJSON *mock_last_response = NULL;

/* Hand the captured response to the caller, who becomes responsible for deleting it. */
cJSON *mock_json_utils_take_response(void)
{
    cJSON *resp = mock_last_response;
    mock_last_response = NULL;
    return resp;
}

void mock_json_utils_reset(void)
{
    if (mock_last_response != NULL) {
        cJSON_Delete(mock_last_response);
        mock_last_response = NULL;
    }
}

cJSON *json_utils_receive_json(httpd_req_t *req)
{
    (void)req;
    return NULL;
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
    (void)error_message;
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
