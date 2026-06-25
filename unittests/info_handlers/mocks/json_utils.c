/* json_utils mock for the info_handlers unit test. The HTTP handlers in the
 * translation unit call send_response/send_error; the function under test does
 * not. These stubs free the response JSON to mirror the real API's ownership so
 * no leak is reported when a handler is exercised. */

#include "json_utils.h"

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
    if (resp_json != NULL) {
        cJSON_Delete(resp_json);
    }
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
