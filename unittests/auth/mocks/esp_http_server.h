#pragma once

/* Minimal stub of esp_http_server.h for the auth unit tests. auth.c only needs the request
 * type and the few response helpers below; the HTTP transport itself is not exercised here. */

#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>
#include <sys/types.h>
#include "esp_err.h"

/* auth.c gets these from the ESP-IDF header chain, which it reaches through this very header
 * (auth.h includes esp_http_server.h first). The mock chain has to hand them over at the same
 * point: PRIu32 for its log/format strings, RTC_NOINIT_ATTR for the session backup buffer and
 * esp_reset_reason() for the boot-time session restore. */
#include "esp_attr.h"
#include "esp_system.h"

typedef void *httpd_handle_t;

typedef struct httpd_req httpd_req_t;

struct httpd_req {
    httpd_handle_t handle;
    int            method;
    char           uri[100];
    size_t         content_len;
    void          *aux;
    void          *user_ctx;
};

esp_err_t httpd_req_get_cookie_val(httpd_req_t *req, const char *cookie_name,
                                   char *val, size_t *val_size);
esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *field, const char *value);
esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *status);
esp_err_t httpd_resp_send(httpd_req_t *req, const char *buf, ssize_t buf_len);
