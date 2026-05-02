#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_err.h"

/* Minimal esp_http_server stub for sniffer unit tests.
 * Only the types and functions referenced by sniffer.c are defined here.
 * httpd_handle_t is already provided by sniffer.h in __unittest_env__, so it
 * must NOT be redefined here. */

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

typedef struct httpd_req {
    httpd_handle_t handle;
    int method;
    char uri[128];
    size_t content_len;
    void *aux;
    void *user_ctx;
} httpd_req_t;

typedef struct httpd_uri {
    const char *uri;
    enum http_method method;
    esp_err_t (*handler)(httpd_req_t *r);
    void *user_ctx;
    bool is_websocket;
} httpd_uri_t;

typedef enum {
    HTTPD_WS_TYPE_CONTINUE = 0,
    HTTPD_WS_TYPE_TEXT,
    HTTPD_WS_TYPE_BINARY,
    HTTPD_WS_TYPE_CLOSE,
    HTTPD_WS_TYPE_PING,
    HTTPD_WS_TYPE_PONG,
} httpd_ws_type_t;

typedef struct httpd_ws_frame {
    bool final;
    bool fragmented;
    httpd_ws_type_t type;
    uint8_t *payload;
    size_t len;
} httpd_ws_frame_t;

static inline esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri_handler)
{
    (void)handle;
    (void)uri_handler;
    return ESP_OK;
}

static inline esp_err_t httpd_ws_recv_frame(httpd_req_t *req, httpd_ws_frame_t *frame, size_t max_len)
{
    (void)req;
    (void)frame;
    (void)max_len;
    return ESP_OK;
}

static inline esp_err_t httpd_ws_send_frame_async(httpd_handle_t handle, int fd, httpd_ws_frame_t *frame)
{
    (void)handle;
    (void)fd;
    (void)frame;
    return ESP_OK;
}

static inline int httpd_req_to_sockfd(httpd_req_t *req)
{
    (void)req;
    return 0;
}

static inline esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type)
{
    (void)req;
    (void)type;
    return ESP_OK;
}

static inline esp_err_t httpd_resp_send(httpd_req_t *req, const char *buf, ssize_t buf_len)
{
    (void)req;
    (void)buf;
    (void)buf_len;
    return ESP_OK;
}
