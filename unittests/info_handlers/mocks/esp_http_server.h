#pragma once

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
    void          *user_ctx;
};

#define HTTPD_MAX_URI_LEN 512
