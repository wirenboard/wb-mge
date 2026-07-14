#include "esp_http_server.h"

#include <string.h>
#include <stddef.h>

/* ---- Mock state definitions ---- */

/* Accumulated response body — null-terminated string, up to 4095 useful bytes */
char         mock_http_resp_buf[4096];

/* Count of httpd_resp_send() calls */
int          mock_http_resp_send_called;

/* Count of httpd_resp_send_chunk() calls */
int          mock_http_resp_send_chunk_called;

/* Last content-type string set via httpd_resp_set_type() */
const char  *mock_http_resp_set_type_last;

/* Last header field name set via httpd_resp_set_hdr() */
const char  *mock_http_resp_set_hdr_last_field;

/* Last header value set via httpd_resp_set_hdr() */
const char  *mock_http_resp_set_hdr_last_value;

/* Status line from the last httpd_resp_set_status() call */
const char *mock_http_resp_status_last;

/* Optional hook invoked at the START of each data-carrying httpd_resp_send_chunk()
 * call, with the 1-based chunk index. Lets tests inject an action (e.g. a cache
 * clear/disable) mid-stream — exactly when the handler has released the mutex —
 * to exercise concurrent-mutation paths. NULL = no hook. */
void (*mock_http_chunk_hook)(int chunk_index) = NULL;

/* ---- Reset ---- */

/* Zero all mock state — call in setUp() before each handler test. */
void mock_http_reset(void)
{
    memset(mock_http_resp_buf, 0, sizeof(mock_http_resp_buf));
    mock_http_resp_send_called       = 0;
    mock_http_resp_send_chunk_called = 0;
    mock_http_resp_set_type_last     = NULL;
    mock_http_resp_set_hdr_last_field = NULL;
    mock_http_resp_set_hdr_last_value = NULL;
    mock_http_chunk_hook              = NULL;
    mock_http_resp_status_last        = NULL;
}

/* ---- Helper: append bytes to the accumulation buffer ---- */

/* Appends up to `len` bytes from `buf` to mock_http_resp_buf.
 * If len == -1 the buffer is treated as a null-terminated string.
 * Silently truncates if the buffer would overflow. */
static void mock_http_append(const char *buf, ssize_t len)
{
    if (buf == NULL) {
        return; /* NULL buf signals "end of chunked transfer", nothing to append */
    }

    size_t append_len;
    if (len == (ssize_t)(-1)) {
        append_len = strlen(buf);
    } else {
        append_len = (size_t)len;
    }

    /* Current fill level (excluding null terminator) */
    size_t current_len = strlen(mock_http_resp_buf);
    size_t space_left  = sizeof(mock_http_resp_buf) - current_len - 1u;

    if (append_len > space_left) {
        append_len = space_left; /* truncate — enough for any realistic test payload */
    }

    memcpy(mock_http_resp_buf + current_len, buf, append_len);
    mock_http_resp_buf[current_len + append_len] = '\0';
}

/* ---- Mock implementations ---- */

esp_err_t httpd_register_uri_handler(httpd_handle_t handle,
                                     const httpd_uri_t *uri_handler)
{
    (void)handle;
    (void)uri_handler;
    return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type)
{
    (void)r;
    mock_http_resp_set_type_last = type;
    return ESP_OK;
}

esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *field,
                              const char *value)
{
    (void)r;
    mock_http_resp_set_hdr_last_field = field;
    mock_http_resp_set_hdr_last_value = value;
    return ESP_OK;
}

/* httpd_resp_send() replaces the accumulated buffer with the new content.
 * This matches the ESP-IDF semantics: a non-chunked send is a complete response. */
esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t buf_len)
{
    (void)r;
    mock_http_resp_send_called++;

    /* Replace the buffer (non-chunked: overwrites any prior content) */
    memset(mock_http_resp_buf, 0, sizeof(mock_http_resp_buf));
    mock_http_append(buf, buf_len);

    return ESP_OK;
}

/* httpd_resp_send_chunk() appends a chunk to the accumulated buffer.
 * A NULL buf signals the end of the chunked transfer — the counter is NOT
 * incremented for the terminating call, only for real data chunks. */
esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf,
                                ssize_t buf_len)
{
    (void)r;
    if (buf != NULL) {
        mock_http_resp_send_chunk_called++;  /* count only data-carrying chunks */
        if (mock_http_chunk_hook != NULL) {
            mock_http_chunk_hook(mock_http_resp_send_chunk_called);
        }
    }
    mock_http_append(buf, buf_len);
    return ESP_OK;
}

esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status)
{
    (void)r;
    mock_http_resp_status_last = status;
    return ESP_OK;
}
