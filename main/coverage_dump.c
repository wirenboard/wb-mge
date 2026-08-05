// gcov coverage-dump HTTP endpoint. Streams the on-target .gcda data so the
// host can rebuild coverage with `gcov-tool merge-stream`. Test-only: compiled
// in coverage builds (idf.py -DCOVERAGE=1) and exposed unauthenticated.
#ifdef COVERAGE_BUILD

#include "coverage_dump.h"

#include <stdlib.h>
#include <gcov.h>

#include "esp_log.h"

static const char *TAG = "coverage";

// .gcov_info section bounds: -fprofile-info-section emits one gcov_info* per
// translation unit here; the start/end symbols are PROVIDEd by coverage.ld.
extern const struct gcov_info *const __gcov_info_start[];
extern const struct gcov_info *const __gcov_info_end[];

typedef struct {
    httpd_req_t *req;
    void *scratch;          // last buffer handed to __gcov_info_to_gcda
    bool failed;            // sticky: a chunk send failed
} coverage_dump_ctx_t;

// Stream one binary chunk of the gcfn/gcda data to the HTTP client.
static void coverage_dump_chunk(const void *data, unsigned length, void *arg)
{
    coverage_dump_ctx_t *ctx = (coverage_dump_ctx_t *)arg;
    if (ctx->failed) {
        return;
    }
    if (httpd_resp_send_chunk(ctx->req, (const char *)data, length) != ESP_OK) {
        ctx->failed = true;
    }
}

// Filename callback: emit the gcfn record that precedes each gcda record.
static void coverage_dump_filename(const char *filename, void *arg)
{
    __gcov_filename_to_gcfn(filename, coverage_dump_chunk, arg);
}

// Scratch allocator for __gcov_info_to_gcda. Frees the previous block so the
// outstanding allocation is bounded to one buffer.
static void *coverage_dump_allocate(unsigned length, void *arg)
{
    coverage_dump_ctx_t *ctx = (coverage_dump_ctx_t *)arg;
    free(ctx->scratch);
    ctx->scratch = malloc(length);
    if (ctx->scratch == NULL) {
        ctx->failed = true;     // out of memory: flag the dump as failed
    }
    return ctx->scratch;
}

// GET /gcov: stream concatenated (gcfn + gcda) records for every instrumented
// translation unit. Rebuild on the host with `xtensa-esp-elf-gcov-tool merge-stream`.
static esp_err_t coverage_dump_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/octet-stream");

    coverage_dump_ctx_t ctx = { .req = req, .scratch = NULL, .failed = false };
    const struct gcov_info *const *info = __gcov_info_start;
    unsigned count = 0;
    while (info != __gcov_info_end) {
        if ((*info) != NULL) {
            __gcov_info_to_gcda(*info, coverage_dump_filename, coverage_dump_chunk,
                                coverage_dump_allocate, &ctx);
            free(ctx.scratch);
            ctx.scratch = NULL;
            count++;
        }
        info++;
    }

    ESP_LOGI(TAG, "Coverage dump: %u translation units streamed", count);

    httpd_resp_send_chunk(req, NULL, 0);    // terminate the chunked response
    return ctx.failed ? ESP_FAIL : ESP_OK;
}

static const httpd_uri_t coverage_dump_get = {
    .uri = "/gcov",
    .method = HTTP_GET,
    .handler = coverage_dump_get_handler,
    .user_ctx = NULL,
};

void coverage_dump_register_handlers(httpd_handle_t server)
{
    httpd_register_uri_handler(server, &coverage_dump_get);
    ESP_LOGW(TAG, "Coverage endpoint registered: GET /gcov (coverage build)");
}

#endif /* COVERAGE_BUILD */
