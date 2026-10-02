#pragma once

/*
 * The one thing fp_acs.h needs from esp_http_server.h on the host: the handle type in the
 * prototype of fp_http_register_handlers(). None of the fp_acs suites builds fp_http.c.
 */

typedef void *httpd_handle_t;
