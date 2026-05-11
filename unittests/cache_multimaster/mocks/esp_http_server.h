#pragma once

/* Minimal stub for esp_http_server.h used in the cache_multimaster unit test.
 * Only the types and functions referenced by cache_multimaster.c are defined.
 * Non-inline implementations live in mocks/esp_http_server.c so that the mock
 * can maintain mutable state (accumulated response buffer, call counters, etc.) */

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

/* ---- Mock state (defined in esp_http_server.c) ---- */

/* Accumulated response body from httpd_resp_send / httpd_resp_send_chunk calls */
extern char         mock_http_resp_buf[4096];

/* Count of httpd_resp_send() calls */
extern int          mock_http_resp_send_called;

/* Count of httpd_resp_send_chunk() calls */
extern int          mock_http_resp_send_chunk_called;

/* Last content-type string set via httpd_resp_set_type() */
extern const char  *mock_http_resp_set_type_last;

/* Last header field name set via httpd_resp_set_hdr() */
extern const char  *mock_http_resp_set_hdr_last_field;

/* Last header field value set via httpd_resp_set_hdr() */
extern const char  *mock_http_resp_set_hdr_last_value;

/* Reset all mock state — call in setUp() before each test */
void mock_http_reset(void);

/* ---- Mock function prototypes ---- */

esp_err_t httpd_register_uri_handler(httpd_handle_t handle,
                                     const httpd_uri_t *uri_handler);

esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type);

esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *field,
                              const char *value);

esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t buf_len);

esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf,
                                ssize_t buf_len);
