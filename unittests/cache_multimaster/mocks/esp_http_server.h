#pragma once

/* Minimal stub for esp_http_server.h used in the cache_multimaster unit test.
 * Only the types and functions referenced by cache_multimaster.c are defined. */

#include <stdint.h>
#include <sys/types.h>
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

/* Stub implementations — the HTTP layer is not exercised by CM-U-001. */
static inline esp_err_t httpd_register_uri_handler(httpd_handle_t handle,
                                                    const httpd_uri_t *uri_handler)
{
    (void)handle;
    (void)uri_handler;
    return ESP_OK;
}

static inline esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type)
{
    (void)r;
    (void)type;
    return ESP_OK;
}

static inline esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *field, const char *value)
{
    (void)r;
    (void)field;
    (void)value;
    return ESP_OK;
}

static inline esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t buf_len)
{
    (void)r;
    (void)buf;
    (void)buf_len;
    return ESP_OK;
}

static inline esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf, ssize_t buf_len)
{
    (void)r;
    (void)buf;
    (void)buf_len;
    return ESP_OK;
}
