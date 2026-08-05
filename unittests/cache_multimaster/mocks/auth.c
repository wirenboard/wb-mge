#include "auth.h"

/* Stub implementation of auth_middleware_check for cache_multimaster unit tests.
 * Always returns true so that HTTP handler logic under test is reachable. */
bool auth_middleware_check(httpd_req_t *req)
{
    return true;
}
