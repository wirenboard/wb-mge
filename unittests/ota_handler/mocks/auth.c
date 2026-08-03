/* auth mock for the ota_handler unit tests. The real auth_middleware_check() answers the request
 * itself when the session is missing (401 + empty body); the mock does the same, so a test can
 * check WHICH answer the caller got and thereby that the OTA state guard sits after the
 * authentication check, not before it. */

#include "auth.h"
#include "esp_http_server.h"
#include "ota_mocks.h"

static bool auth_result = true;

int mock_auth_middleware_check_call_count = 0;

void mock_auth_set_result(bool result)
{
    auth_result = result;
}

void mock_auth_reset(void)
{
    auth_result = true;
    mock_auth_middleware_check_call_count = 0;
}

bool auth_middleware_check(httpd_req_t *req)
{
    mock_auth_middleware_check_call_count++;
    if (auth_result) {
        return true;
    }
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_send(req, NULL, 0);
    return false;
}
