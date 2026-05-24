#pragma once

/* Minimal stub for esp_http_server.h used in port_manager unit tests.
 * cache_multimaster.h and auth.h pull in esp_http_server.h for httpd_handle_t
 * and httpd_req_t; we only need the types — the HTTP layer is not exercised
 * by port_manager tests. */

#include <stdint.h>
#include "esp_err.h"

typedef void *httpd_handle_t;

enum http_method {
    HTTP_OPTIONS = 0,
    HTTP_GET,
    HTTP_HEAD,
    HTTP_POST,
    HTTP_PUT,
    HTTP_PATCH,
    HTTP_DELETE,
    HTTP_ANY
};

typedef struct httpd_req httpd_req_t;

struct httpd_req {
    httpd_handle_t handle;
    int            method;
    char           uri[100];
    size_t         content_len;
    void          *aux;
    void          *user_ctx;
};

typedef struct httpd_uri {
    const char      *uri;
    enum http_method method;
    esp_err_t      (*handler)(httpd_req_t *r);
    void            *user_ctx;
} httpd_uri_t;

static inline esp_err_t httpd_register_uri_handler(httpd_handle_t handle,
                                                    const httpd_uri_t *uri_handler)
{
    (void)handle;
    (void)uri_handler;
    return 0; /* ESP_OK */
}
