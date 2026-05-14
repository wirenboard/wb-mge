#include <stdbool.h>
#include "sniffer.h"          /* provides httpd_handle_t in __unittest_env__ */
#include "esp_http_server.h"  /* provides httpd_req_t */

/* Stub implementation of auth_middleware_check for sniffer unit tests.
 * Always returns true so that HTTP handler logic under test is reachable.
 * auth.h is intentionally not included here: it pulls in the real
 * esp_http_server.h which conflicts with the test-env type stubs. */
bool auth_middleware_check(httpd_req_t *req)
{
    (void)req;
    return true;
}
