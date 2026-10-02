#include "esp_http_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "trace.h"

#define ESP_HTTP_FAKE_ANSWERS_MAX       16
#define ESP_HTTP_FAKE_ANSWER_MAX        (20 * 1024)
#define ESP_HTTP_FAKE_DEFAULT_BODY      "{\"ok\":true,\"result\":[]}"
#define ESP_HTTP_FAKE_OK                200
#define ESP_HTTP_FAKE_BOT_PATH          "/bot"
#define ESP_HTTP_FAKE_FILLER            'x'

struct esp_http_client {
    int                      status;
    bool                     post;
    char                     url[ESP_HTTP_FAKE_URL_MAX];
    char                     body[ESP_HTTP_FAKE_BODY_MAX];
    char                     content_type[64];
    http_event_handle_cb     handler;
};

typedef struct {
    esp_err_t err;
    int       status;
    char     *body;
    size_t    len;
} esp_http_fake_answer_t;

esp_http_fake_request_t  esp_http_fake_requests[ESP_HTTP_FAKE_REQUESTS_MAX];
int                      esp_http_fake_request_count = 0;
int                      esp_http_fake_init_calls = 0;
int                      esp_http_fake_close_calls = 0;
int                      esp_http_fake_cleanup_calls = 0;
esp_http_client_config_t esp_http_fake_config;
char                     esp_http_fake_config_url[ESP_HTTP_FAKE_URL_MAX];
bool                     esp_http_fake_init_fails = false;
size_t                   esp_http_fake_chunk = 512;

static esp_http_fake_answer_t esp_http_fake_answers[ESP_HTTP_FAKE_ANSWERS_MAX];
static unsigned               esp_http_fake_answers_len;
static unsigned               esp_http_fake_answers_next;

static void esp_http_fake_queue(esp_err_t err, int status, char *body, size_t len)
{
    if (esp_http_fake_answers_len >= ESP_HTTP_FAKE_ANSWERS_MAX) {
        free(body);
        return;
    }
    esp_http_fake_answer_t *answer = &esp_http_fake_answers[esp_http_fake_answers_len++];
    answer->err = err;
    answer->status = status;
    answer->body = body;
    answer->len = len;
}

void esp_http_fake_respond(esp_err_t err, int status, const char *body)
{
    size_t len = (body != NULL) ? strlen(body) : 0;
    char *copy = malloc(len + 1);
    memcpy(copy, (body != NULL) ? body : "", len + 1);
    esp_http_fake_queue(err, status, copy, len);
}

void esp_http_fake_respond_long(int status, const char *body, size_t total)
{
    if (total > ESP_HTTP_FAKE_ANSWER_MAX) {
        total = ESP_HTTP_FAKE_ANSWER_MAX;
    }
    char *copy = malloc(total + 1);
    memset(copy, ESP_HTTP_FAKE_FILLER, total);
    size_t len = strlen(body);
    memcpy(copy, body, (len < total) ? len : total);
    copy[total] = '\0';
    esp_http_fake_queue(ESP_OK, status, copy, total);
}

void esp_http_fake_reset(void)
{
    for (unsigned i = 0; i < esp_http_fake_answers_len; i++) {
        free(esp_http_fake_answers[i].body);
    }
    memset(esp_http_fake_answers, 0, sizeof(esp_http_fake_answers));
    esp_http_fake_answers_len = 0;
    esp_http_fake_answers_next = 0;
    memset(esp_http_fake_requests, 0, sizeof(esp_http_fake_requests));
    esp_http_fake_request_count = 0;
    esp_http_fake_init_calls = 0;
    esp_http_fake_close_calls = 0;
    esp_http_fake_cleanup_calls = 0;
    memset(&esp_http_fake_config, 0, sizeof(esp_http_fake_config));
    esp_http_fake_config_url[0] = '\0';
    esp_http_fake_init_fails = false;
    esp_http_fake_chunk = 512;
}

const char *esp_http_fake_method(const esp_http_fake_request_t *request)
{
    const char *bot = strstr(request->url, ESP_HTTP_FAKE_BOT_PATH);
    const char *slash = (bot != NULL) ? strchr(bot + 1, '/') : NULL;
    return (slash != NULL) ? (slash + 1) : request->url;
}

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    esp_http_fake_init_calls++;
    esp_http_fake_config = *config;
    snprintf(esp_http_fake_config_url, sizeof(esp_http_fake_config_url), "%s", config->url);
    if (esp_http_fake_init_fails) {
        return NULL;
    }
    struct esp_http_client *client = calloc(1, sizeof(*client));
    client->handler = config->event_handler;
    snprintf(client->url, sizeof(client->url), "%s", config->url);
    return client;
}

esp_err_t esp_http_client_set_url(esp_http_client_handle_t client, const char *url)
{
    snprintf(client->url, sizeof(client->url), "%s", url);
    return ESP_OK;
}

esp_err_t esp_http_client_set_method(esp_http_client_handle_t client, esp_http_client_method_t method)
{
    client->post = (method == HTTP_METHOD_POST);
    return ESP_OK;
}

esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value)
{
    if (strcmp(key, "Content-Type") == 0) {
        snprintf(client->content_type, sizeof(client->content_type), "%s", value);
    }
    return ESP_OK;
}

esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client, const char *data, int len)
{
    if (data == NULL) {
        client->body[0] = '\0';
        // As the real client: a POST field reset also drops the Content-Type it implied.
        client->content_type[0] = '\0';
        return ESP_OK;
    }
    snprintf(client->body, sizeof(client->body), "%.*s", len, data);
    // As the real client: a POST field without a Content-Type set before it is sent as a form.
    if (client->content_type[0] == '\0') {
        snprintf(client->content_type, sizeof(client->content_type), "application/x-www-form-urlencoded");
    }
    return ESP_OK;
}

esp_err_t esp_http_client_perform(esp_http_client_handle_t client)
{
    if (esp_http_fake_request_count < ESP_HTTP_FAKE_REQUESTS_MAX) {
        esp_http_fake_request_t *request = &esp_http_fake_requests[esp_http_fake_request_count];
        request->post = client->post;
        snprintf(request->url, sizeof(request->url), "%s", client->url);
        snprintf(request->body, sizeof(request->body), "%s", client->post ? client->body : "");
        snprintf(request->content_type, sizeof(request->content_type), "%s", client->content_type);
        // The method name only, without the query: the URL holds the token.
        char method[64];
        snprintf(method, sizeof(method), "%s", esp_http_fake_method(request));
        method[strcspn(method, "?")] = '\0';
        trace_add("%s %s", client->post ? "POST" : "GET", method);
    }
    esp_http_fake_request_count++;

    esp_http_fake_answer_t fallback = {ESP_OK, ESP_HTTP_FAKE_OK, (char *)ESP_HTTP_FAKE_DEFAULT_BODY,
                                       strlen(ESP_HTTP_FAKE_DEFAULT_BODY)};
    const esp_http_fake_answer_t *answer = &fallback;
    if (esp_http_fake_answers_next < esp_http_fake_answers_len) {
        answer = &esp_http_fake_answers[esp_http_fake_answers_next++];
    }
    if (answer->err != ESP_OK) {
        return answer->err;
    }
    client->status = answer->status;
    for (size_t pos = 0; pos < answer->len; pos += esp_http_fake_chunk) {
        size_t n = answer->len - pos;
        if (n > esp_http_fake_chunk) {
            n = esp_http_fake_chunk;
        }
        esp_http_client_event_t event = {
            .event_id = HTTP_EVENT_ON_DATA,
            .client = client,
            .data = answer->body + pos,
            .data_len = (int)n,
        };
        if (client->handler != NULL) {
            client->handler(&event);
        }
    }
    return ESP_OK;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client)
{
    return client->status;
}

esp_err_t esp_http_client_close(esp_http_client_handle_t client)
{
    (void)client;
    esp_http_fake_close_calls++;
    return ESP_OK;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client)
{
    esp_http_fake_cleanup_calls++;
    free(client);
    return ESP_OK;
}
