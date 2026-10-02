#pragma once

/*
 * esp_http_client.h, reduced to what tg_bot uses, over a fake server. Every perform() is logged
 * (method, URL, body) and added to the trace as "GET <method>" or "POST <method>", where <method>
 * is the URL after "/bot<token>/" - so the token never shows in a trace. Its answer is the next one
 * queued with esp_http_fake_respond(), delivered to the event handler in chunks as the real client
 * does, or {"ok":true,"result":[]} once the queue is used up. The error codes are ESP-IDF's own.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define ESP_ERR_HTTP_BASE               (0x7000)
#define ESP_ERR_HTTP_CONNECT            (ESP_ERR_HTTP_BASE + 2)
#define ESP_ERR_HTTP_FETCH_HEADER       (ESP_ERR_HTTP_BASE + 4)
#define ESP_ERR_HTTP_CONNECTION_CLOSED  (ESP_ERR_HTTP_BASE + 8)

typedef struct esp_http_client *esp_http_client_handle_t;

typedef enum {
    HTTP_EVENT_ERROR = 0,
    HTTP_EVENT_ON_CONNECTED,
    HTTP_EVENT_HEADERS_SENT,
    HTTP_EVENT_ON_HEADER,
    HTTP_EVENT_ON_DATA,
    HTTP_EVENT_ON_FINISH,
    HTTP_EVENT_DISCONNECTED,
    HTTP_EVENT_REDIRECT,
} esp_http_client_event_id_t;

typedef struct esp_http_client_event {
    esp_http_client_event_id_t event_id;
    esp_http_client_handle_t   client;
    void                      *data;
    int                        data_len;
    void                      *user_data;
    char                      *header_key;
    char                      *header_value;
} esp_http_client_event_t;

typedef esp_err_t (*http_event_handle_cb)(esp_http_client_event_t *evt);

typedef enum {
    HTTP_METHOD_GET = 0,
    HTTP_METHOD_POST,
} esp_http_client_method_t;

typedef struct {
    const char          *url;
    int                  timeout_ms;
    esp_err_t          (*crt_bundle_attach)(void *conf);
    http_event_handle_cb event_handler;
} esp_http_client_config_t;

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t esp_http_client_set_url(esp_http_client_handle_t client, const char *url);
esp_err_t esp_http_client_set_method(esp_http_client_handle_t client, esp_http_client_method_t method);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value);
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client, const char *data, int len);
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);
int       esp_http_client_get_status_code(esp_http_client_handle_t client);
esp_err_t esp_http_client_close(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);

// ── Test side ────────────────────────────────────────────────────────────────────────────

#define ESP_HTTP_FAKE_REQUESTS_MAX      32
#define ESP_HTTP_FAKE_URL_MAX           256
#define ESP_HTTP_FAKE_BODY_MAX          2048

typedef struct {
    bool post;
    char url[ESP_HTTP_FAKE_URL_MAX];
    char body[ESP_HTTP_FAKE_BODY_MAX];          // the POST body, empty for a GET
    char content_type[64];                      // the Content-Type header a POST set
} esp_http_fake_request_t;

extern esp_http_fake_request_t esp_http_fake_requests[ESP_HTTP_FAKE_REQUESTS_MAX];
extern int                     esp_http_fake_request_count;
extern int                     esp_http_fake_init_calls;
extern int                     esp_http_fake_close_calls;
extern int                     esp_http_fake_cleanup_calls;
extern esp_http_client_config_t esp_http_fake_config;           // of the last init
extern char                    esp_http_fake_config_url[ESP_HTTP_FAKE_URL_MAX];
extern bool                    esp_http_fake_init_fails;
// The data events of an answer carry at most this many bytes each.
extern size_t                  esp_http_fake_chunk;

// Queues the answer to the next request: err != ESP_OK makes perform() fail with it (status and
// body unused). body is copied; a longer one than the fake holds is cut.
void esp_http_fake_respond(esp_err_t err, int status, const char *body);
// Queues an answer whose body is body followed by filler up to total bytes.
void esp_http_fake_respond_long(int status, const char *body, size_t total);
void esp_http_fake_reset(void);
// The method part of a request URL: what follows "/bot<token>/".
const char *esp_http_fake_method(const esp_http_fake_request_t *request);
