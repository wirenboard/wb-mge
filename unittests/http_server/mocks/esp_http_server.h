#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include "esp_err.h"

#define HTTPD_MAX_URI_LEN                       1024
#define MAX_URI_HANDLERS                        20

#define STACK_SIZE                              1024 * 6
#define MAX_OPEN_SOCKETS                        12
#define WEB_PORT_DEFAULT                        80

#define tskIDLE_PRIORITY                        0
#define tskNO_AFFINITY                          -1

typedef void* httpd_handle_t;
typedef void (*httpd_free_ctx_fn_t)(void *ctx);
typedef esp_err_t (*httpd_close_func_t)(httpd_handle_t hd, int sockfd);
typedef esp_err_t (*httpd_uri_match_func_t)(const char *reference_uri, const char *uri_to_match, size_t match_upto);
typedef int BaseType_t;
typedef esp_err_t (*httpd_open_func_t)(httpd_handle_t hd, int sockfd);

enum http_method
{
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

typedef struct httpd_config {
    unsigned    task_priority;
    size_t      stack_size;
    BaseType_t  core_id;
    uint32_t    task_caps;
    uint16_t    server_port;
    uint16_t    ctrl_port;
    uint16_t    max_open_sockets;
    uint16_t    max_uri_handlers;
    uint16_t    max_resp_headers;
    uint16_t    backlog_conn;
    bool        lru_purge_enable;
    uint16_t    recv_wait_timeout;
    uint16_t    send_wait_timeout;
    void * global_user_ctx;
    httpd_free_ctx_fn_t global_user_ctx_free_fn;
    httpd_open_func_t open_fn;
    httpd_close_func_t close_fn;
    httpd_uri_match_func_t uri_match_fn;
    bool enable_so_linger;
    int linger_timeout;
} httpd_config_t;

typedef struct httpd_uri {
    const char       *uri;
    enum http_method  method;
    esp_err_t (*handler)(httpd_req_t *r);
    void *user_ctx;
} httpd_uri_t;

struct httpd_req {
    httpd_handle_t handle;
    int method;
    const char uri[HTTPD_MAX_URI_LEN + 1];
    size_t content_len;
    void *aux;
    void *user_ctx;
};

#define HTTPD_DEFAULT_CONFIG() {                \
    .task_priority      = tskIDLE_PRIORITY+5,   \
    .stack_size         = STACK_SIZE,           \
    .core_id            = tskNO_AFFINITY,       \
    .server_port        = WEB_PORT_DEFAULT,     \
    .ctrl_port          = 32768,                \
    .max_open_sockets   = MAX_OPEN_SOCKETS,     \
    .max_uri_handlers   = MAX_URI_HANDLERS,     \
    .max_resp_headers   = 8,                    \
    .backlog_conn       = 5,                    \
    .lru_purge_enable   = false,                \
    .recv_wait_timeout  = 5,                    \
    .send_wait_timeout  = 5,                    \
    .global_user_ctx = NULL,                    \
    .global_user_ctx_free_fn = NULL,            \
    .open_fn = NULL,                            \
    .close_fn = NULL,                           \
    .uri_match_fn = NULL                        \
}

extern int mock_httpd_start_call_count;
extern int mock_httpd_register_uri_handler_call_count;
extern int mock_wifi_scan_init_call_count;
extern int mock_auth_init_call_count;
extern esp_err_t mock_httpd_start_return_value;
extern esp_err_t mock_wifi_scan_init_return_value;
extern esp_err_t mock_auth_init_return_value;
extern httpd_config_t mock_captured_config;
extern char mock_registered_uris[MAX_URI_HANDLERS][HTTPD_MAX_URI_LEN];
extern httpd_handle_t mock_server_handle;

extern int mock_httpd_resp_set_type_call_count;
extern int mock_httpd_resp_set_hdr_call_count;
extern int mock_httpd_resp_send_call_count;
extern char mock_last_content_type[128];
extern char mock_last_header_field[128];
extern char mock_last_header_value[128];

typedef struct {
    char uri[HTTPD_MAX_URI_LEN];
    enum http_method method;
    esp_err_t (*handler)(httpd_req_t *r);
    void *user_ctx;
    bool registered;
} mock_uri_registry_entry_t;

extern mock_uri_registry_entry_t mock_uri_registry[MAX_URI_HANDLERS];
extern int mock_uri_registry_count;

bool mock_simulate_http_request(enum http_method method, const char* uri);
httpd_req_t mock_create_request(const char* uri, enum http_method method);

void esp_http_server_init(void);

esp_err_t httpd_start(httpd_handle_t *handle, const httpd_config_t *config);
esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri_handler);

esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type);
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *field, const char *value);
esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t buf_len);
