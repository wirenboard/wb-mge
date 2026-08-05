#include "esp_http_server.h"

#include <string.h>

/* Runaway guard. With an "always" error the only thing that can end the receive loop is the stall
 * limit inside the code under test, so a regression that removes that limit would hang the whole
 * suite. Past this many calls the mock reports a hard error instead, turning the hang into a failed
 * assertion on the call count. Far above any number a healthy handler can produce. */
#define MOCK_HTTP_RECV_CALL_CAP     10000

static const uint8_t *body = NULL;
static size_t body_len = 0;
static size_t body_offset = 0;

static int recv_error = 0;
static int recv_error_at_call = 0;
static int recv_error_always = 0;

static const int *recv_script = NULL;
static size_t recv_script_len = 0;

static size_t recv_chunk_limit = 0;

static const char *content_type = NULL;

int         mock_httpd_req_recv_call_count = 0;
int         mock_httpd_resp_send_call_count = 0;
const char *mock_httpd_resp_last_status = NULL;

void mock_http_set_body(const uint8_t *new_body, size_t len)
{
    body = new_body;
    body_len = len;
    body_offset = 0;
}

void mock_http_set_recv_error(int call_number, int error)
{
    recv_error_at_call = call_number;
    recv_error = error;
}

void mock_http_set_recv_error_always(int error)
{
    recv_error_always = error;
}

void mock_http_set_recv_script(const int *outcomes, size_t count)
{
    recv_script = outcomes;
    recv_script_len = count;
}

void mock_http_set_recv_chunk_limit(size_t max_bytes)
{
    recv_chunk_limit = max_bytes;
}

void mock_http_set_content_type(const char *new_content_type)
{
    content_type = new_content_type;
}

void mock_http_reset(void)
{
    body = NULL;
    body_len = 0;
    body_offset = 0;
    recv_error = 0;
    recv_error_at_call = 0;
    recv_error_always = 0;
    recv_script = NULL;
    recv_script_len = 0;
    recv_chunk_limit = 0;
    content_type = NULL;
    mock_httpd_req_recv_call_count = 0;
    mock_httpd_resp_send_call_count = 0;
    mock_httpd_resp_last_status = NULL;
}

int httpd_req_recv(httpd_req_t *req, char *buf, size_t buf_len)
{
    (void)req;
    mock_httpd_req_recv_call_count++;

    if (mock_httpd_req_recv_call_count > MOCK_HTTP_RECV_CALL_CAP) {
        return HTTPD_SOCK_ERR_FAIL;
    }

    if ((recv_error_at_call != 0) && (mock_httpd_req_recv_call_count == recv_error_at_call)) {
        return recv_error;
    }

    if (recv_error_always != 0) {
        return recv_error_always;
    }

    if ((recv_script != NULL) && ((size_t)mock_httpd_req_recv_call_count <= recv_script_len)) {
        int outcome = recv_script[mock_httpd_req_recv_call_count - 1];
        if (outcome != MOCK_RECV_DATA) {
            return outcome;
        }
    }

    size_t available = (body_len > body_offset) ? (body_len - body_offset) : 0;
    if (available == 0) {
        return 0;   // The real API reports a closed connection the same way
    }

    size_t wanted = buf_len;
    if ((recv_chunk_limit != 0) && (recv_chunk_limit < wanted)) {
        wanted = recv_chunk_limit;
    }
    size_t chunk = (available < wanted) ? available : wanted;
    memcpy(buf, &body[body_offset], chunk);
    body_offset += chunk;
    return (int)chunk;
}

esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *req, const char *field, char *val, size_t val_size)
{
    (void)req;
    (void)field;

    if (content_type == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    size_t len = strlen(content_type);
    if (len + 1 > val_size) {
        return ESP_FAIL;
    }
    memcpy(val, content_type, len + 1);
    return ESP_OK;
}

esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *status)
{
    (void)req;
    mock_httpd_resp_last_status = status;
    return ESP_OK;
}

esp_err_t httpd_resp_send(httpd_req_t *req, const char *buf, ssize_t buf_len)
{
    (void)req;
    (void)buf;
    (void)buf_len;
    mock_httpd_resp_send_call_count++;
    return ESP_OK;
}
