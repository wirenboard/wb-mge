#include "fp_console.h"

#if FP_ACS_ENABLED

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "fp_console";

#define FP_CONSOLE_LINE_MAX         256     // a longer line is cut and ends in FP_CONSOLE_CUT_MARK
#define FP_CONSOLE_CUT_MARK         "...\n"
#define FP_CONSOLE_CUT_MARK_LEN     (sizeof(FP_CONSOLE_CUT_MARK) - 1)
#define FP_CONSOLE_LOCK_WAIT_MS     10      // as long as esp_log waits for its own lock
#define FP_CONSOLE_BYTES_PER_KB     1024

_Static_assert(FP_CONSOLE_LINE_MAX < FP_CONSOLE_SIZE, "a line must fit the ring");
_Static_assert(FP_CONSOLE_CUT_MARK_LEN < FP_CONSOLE_LINE_MAX, "the cut mark must fit a line");

// NULL until the capture runs; guards everything below. Static, so that creating it cannot fail.
static StaticSemaphore_t  fp_console_mutex_buf;
static SemaphoreHandle_t  fp_console_mutex;
static char              *fp_console_ring;                          // FP_CONSOLE_SIZE bytes in PSRAM
static size_t             fp_console_head;                          // where the next byte goes
static bool               fp_console_wrapped;                       // full: the oldest byte is at head
static char               fp_console_line[FP_CONSOLE_LINE_MAX];     // the line being formatted

// The vprintf that prints to the UART. Written once, before the ring is used.
static vprintf_like_t     fp_console_prev;


// Under the mutex. len is at most FP_CONSOLE_LINE_MAX, far less than the ring.
static void fp_console_append(const char *text, size_t len)
{
    size_t first = FP_CONSOLE_SIZE - fp_console_head;               // room up to the end of the ring
    if (first > len) {
        first = len;
    }
    memcpy(fp_console_ring + fp_console_head, text, first);
    memcpy(fp_console_ring, text + first, len - first);
    if ((fp_console_head + len) >= FP_CONSOLE_SIZE) {
        fp_console_wrapped = true;
    }
    fp_console_head = (fp_console_head + len) % FP_CONSOLE_SIZE;
}

// The log's vprintf. It runs in every task that logs, some of them with small stacks: the line is
// formatted into a static buffer, and nothing here logs. The UART gets every line first and
// whatever happens next; the ring skips a line rather than hold the task up for more than
// FP_CONSOLE_LOCK_WAIT_MS.
static int fp_console_vprintf(const char *fmt, va_list args)
{
    // esp_log calls this without a lock, so it may run before esp_log_set_vprintf() has returned
    // the previous vprintf to fp_console_start(): until then, that is the default one.
    vprintf_like_t prev = fp_console_prev;
    if (prev == NULL) {
        prev = vprintf;
    }
    va_list uart_args;
    va_copy(uart_args, args);
    int ret = prev(fmt, uart_args);
    va_end(uart_args);

    // An ISR or a critical section cannot wait for the mutex, and before the scheduler runs there
    // is nobody to wait for.
    if (xPortInIsrContext() || (!xPortCanYield()) || (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)) {
        return ret;
    }
    if (xSemaphoreTake(fp_console_mutex, pdMS_TO_TICKS(FP_CONSOLE_LOCK_WAIT_MS)) != pdTRUE) {
        return ret;
    }
    int len = vsnprintf(fp_console_line, sizeof(fp_console_line), fmt, args);
    if (len > 0) {
        if ((size_t)len >= sizeof(fp_console_line)) {
            len = (int)(sizeof(fp_console_line) - 1);
            memcpy(&fp_console_line[(size_t)len - FP_CONSOLE_CUT_MARK_LEN], FP_CONSOLE_CUT_MARK,
                   FP_CONSOLE_CUT_MARK_LEN);
        }
        fp_console_append(fp_console_line, (size_t)len);
    }
    xSemaphoreGive(fp_console_mutex);
    return ret;
}

void fp_console_start(void)
{
    if (fp_console_mutex != NULL) {
        return;                                                     // a second hook would call itself
    }
    fp_console_ring = heap_caps_malloc(FP_CONSOLE_SIZE, MALLOC_CAP_SPIRAM);
    if (fp_console_ring == NULL) {
        ESP_LOGW(TAG, "No PSRAM for the console ring: GET /fp/console stays unavailable");
        return;
    }
    fp_console_mutex = xSemaphoreCreateMutexStatic(&fp_console_mutex_buf);
    fp_console_prev = esp_log_set_vprintf(fp_console_vprintf);
    ESP_LOGI(TAG, "Keeping the last %u KB of the log for GET /fp/console",
             (unsigned)(FP_CONSOLE_SIZE / FP_CONSOLE_BYTES_PER_KB));
}

// Once the ring has wrapped, the oldest line has lost its beginning: the bytes from the oldest one
// up to and including the first newline, which are skipped. 0 when there is no newline at all.
// Under the mutex.
static size_t fp_console_partial_len(void)
{
    const char *tail = fp_console_ring + fp_console_head;
    size_t tail_len = FP_CONSOLE_SIZE - fp_console_head;
    const char *newline = memchr(tail, '\n', tail_len);
    if (newline != NULL) {
        return ((size_t)(newline - tail) + 1);
    }
    newline = memchr(fp_console_ring, '\n', fp_console_head);
    if (newline != NULL) {
        return (tail_len + (size_t)(newline - fp_console_ring) + 1);
    }
    return 0;
}

bool fp_console_copy(char *out, size_t out_size, size_t *len)
{
    *len = 0;
    if ((fp_console_mutex == NULL) || (out == NULL) || (out_size < FP_CONSOLE_SIZE)) {
        return false;
    }

    xSemaphoreTake(fp_console_mutex, portMAX_DELAY);
    size_t start = 0;
    size_t count = fp_console_head;
    if (fp_console_wrapped) {
        size_t skip = fp_console_partial_len();
        start = (fp_console_head + skip) % FP_CONSOLE_SIZE;
        count = FP_CONSOLE_SIZE - skip;
    }
    size_t first = FP_CONSOLE_SIZE - start;                         // up to the end of the ring
    if (first > count) {
        first = count;
    }
    memcpy(out, fp_console_ring + start, first);
    memcpy(out + first, fp_console_ring, count - first);
    xSemaphoreGive(fp_console_mutex);

    *len = count;
    return true;
}

#endif // FP_ACS_ENABLED
