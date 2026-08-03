#pragma once

/* Minimal stub of esp_http_server.h for the ota_handler unit tests. ota_handler.c needs the request
 * type, the body-receive call with its timeout sentinel, one request-header getter and the two
 * response calls the auth and json_utils mocks funnel through. */

#include <stdbool.h>
#include <stddef.h>
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
    /* 32-bit on purpose: on the target size_t is 32 bits, which is what the "%d" format strings in
     * the code under test are written against. A 64-bit size_t here would produce format warnings
     * that exist only on the host. */
    unsigned int   content_len;
    void          *aux;
    void          *user_ctx;
};

/* Error codes httpd_req_recv() reports, same values as ESP-IDF. */
#define HTTPD_SOCK_ERR_FAIL      (-1)
#define HTTPD_SOCK_ERR_INVALID   (-2)
#define HTTPD_SOCK_ERR_TIMEOUT   (-3)

int       httpd_req_recv(httpd_req_t *req, char *buf, size_t buf_len);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *req, const char *field, char *val, size_t val_size);
esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *status);
esp_err_t httpd_resp_send(httpd_req_t *req, const char *buf, ssize_t buf_len);

/* --- mock control and observation ---------------------------------------- */

/* Body the next upload receives, handed out in chunks of whatever size the handler asks for. */
void mock_http_set_body(const uint8_t *body, size_t len);

/* Make the Nth (1-based) httpd_req_recv() call return `error` instead of data. 0 disables it. */
void mock_http_set_recv_error(int call_number, int error);

/* Make EVERY httpd_req_recv() call return `error`, for as long as the handler keeps asking. This is
 * the client that vanished without closing its socket: the receive loop only ever sees timeouts and
 * nothing but its own limit can end it. 0 disables it. */
void mock_http_set_recv_error_always(int error);

/* Outcome of one httpd_req_recv() call in a script: a negative entry is returned to the caller as
 * is (the HTTPD_SOCK_ERR_* sentinels), MOCK_RECV_DATA hands out the next slice of the body. */
#define MOCK_RECV_DATA  0

/* Play `outcomes` back one entry per httpd_req_recv() call; once the script runs out the mock goes
 * back to handing out the body. Lets a test interleave timeouts with accepted data, which no
 * single-call knob can express. The array must outlive the calls (a static or a local in the test).
 * NULL/0 disables it. */
void mock_http_set_recv_script(const int *outcomes, size_t count);

/* Hand out at most this many body bytes per httpd_req_recv() call, so that one upload is spread
 * over several receives. 0 gives the handler as much as it asks for. */
void mock_http_set_recv_chunk_limit(size_t max_bytes);

/* Content-Type the request carries; NULL makes the header absent. */
void mock_http_set_content_type(const char *content_type);

void mock_http_reset(void);

extern int         mock_httpd_req_recv_call_count;
extern int         mock_httpd_resp_send_call_count;
extern const char *mock_httpd_resp_last_status;   /* NULL until a status is set */
