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
    HTTPD_WS_CLIENT_INVALID        = 0x0,
    HTTPD_WS_CLIENT_HTTP           = 0x1,
    HTTPD_WS_CLIENT_WEBSOCKET      = 0x2,
} httpd_ws_client_info_t;

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

/* Return the fd stored in req->aux cast to int; fall back to 42 if aux is NULL.
 * This allows tests to configure a specific fd by setting req->aux. */
static inline int httpd_req_to_sockfd(httpd_req_t *req)
{
    return req->aux ? (int)(intptr_t)req->aux : 42;
}

static inline esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type)
{
    (void)req;
    (void)type;
    return ESP_OK;
}

/* Tracked in esp_http_server_mock.c: the readiness guard in sniffer_ws_handler()
 * reports its refusal through the status line and the body, so a test has to be able
 * to read both back. */
esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *status);
esp_err_t httpd_resp_send(httpd_req_t *req, const char *buf, ssize_t buf_len);

/* Tracked in esp_http_server_mock.c: the sniffer uses it both to reject an
 * unauthenticated upgrade and to evict the previous single-slot WS client. */
esp_err_t httpd_sess_trigger_close(httpd_handle_t handle, int sockfd);

/* Controllable mock state for httpd_ws_get_fd_info and httpd_ws_send_data.
 * Defined in esp_http_server_mock.c; tests use mock_esp_http_server_reset()
 * in setUp() to restore defaults before each test case. */
extern httpd_ws_client_info_t mock_httpd_ws_get_fd_info_return;
extern int mock_httpd_ws_get_fd_info_called;
extern int mock_httpd_ws_get_fd_info_last_fd;

extern esp_err_t mock_httpd_ws_send_data_return;
extern int mock_httpd_ws_send_data_called;
extern httpd_ws_frame_t mock_httpd_ws_send_data_last_frame;
extern int mock_httpd_ws_send_data_last_fd;

extern int mock_httpd_sess_trigger_close_called;
extern int mock_httpd_sess_trigger_close_last_fd;
extern httpd_handle_t mock_httpd_sess_trigger_close_last_handle;

/* Last status line passed to httpd_resp_set_status(), NULL when never called
 * (esp_http_server keeps the caller's pointer, so the mock does the same). */
extern int         mock_httpd_resp_set_status_called;
extern const char *mock_httpd_resp_set_status_last;

extern int  mock_httpd_resp_send_called;
extern char mock_httpd_resp_send_last_body[128];

void mock_esp_http_server_reset(void);

httpd_ws_client_info_t httpd_ws_get_fd_info(httpd_handle_t hd, int fd);
esp_err_t httpd_ws_send_data(httpd_handle_t handle, int fd, httpd_ws_frame_t *frame);
