#include "unity.h"

/*
 * fp_console: the RAM copy of the device log behind GET /fp/console (main/fp_acs/fp_console.h).
 *
 * fp_console.c is #included so that the test can reset the ring between tests. Lines are fed the
 * way esp_log feeds them: through the vprintf fp_console_start() installs (the mock esp_log.h
 * records it), with a "UART" vprintf of the test's as the one it replaced.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

// The FreeRTOS port calls fp_console_vprintf() checks before it takes the ring's mutex. The shared
// mocks do not have them; the states they report are the test's.
#define taskSCHEDULER_SUSPENDED     0
#define taskSCHEDULER_NOT_STARTED   1
#define taskSCHEDULER_RUNNING       2

BaseType_t xPortInIsrContext(void);
BaseType_t xPortCanYield(void);
BaseType_t xTaskGetSchedulerState(void);

#include "fp_console.c"

#if !FP_ACS_ENABLED
    #error "fp_console_test needs FP_ACS_ENABLED: check DEFS in the Makefile"
#endif

#define FP_CONSOLE_TEST_UART_MAX    1024
#define FP_CONSOLE_TEST_LINE_LEN    100             // the lines the wrap tests write, newline included

static BaseType_t fp_console_test_in_isr;
static BaseType_t fp_console_test_can_yield;
static BaseType_t fp_console_test_scheduler;

static char fp_console_test_uart[FP_CONSOLE_TEST_UART_MAX];
static int  fp_console_test_uart_calls;

static char fp_console_test_out[FP_CONSOLE_SIZE + 1];

BaseType_t xPortInIsrContext(void)
{
    return fp_console_test_in_isr;
}

BaseType_t xPortCanYield(void)
{
    return fp_console_test_can_yield;
}

BaseType_t xTaskGetSchedulerState(void)
{
    return fp_console_test_scheduler;
}

// The vprintf that printed to the UART before fp_console_start(): keeps the last line it got.
static int fp_console_test_uart_vprintf(const char *fmt, va_list args)
{
    fp_console_test_uart_calls++;
    return vsnprintf(fp_console_test_uart, sizeof(fp_console_test_uart), fmt, args);
}

// One log line, as esp_log hands it to the installed vprintf.
static int fp_console_test_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static int fp_console_test_log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int ret = mock_esp_log_installed_vprintf(fmt, args);
    va_end(args);
    return ret;
}

// A line of exactly FP_CONSOLE_TEST_LINE_LEN bytes that names its number.
static void fp_console_test_line(unsigned n, char *buf, size_t len)
{
    snprintf(buf, len, "%05u%.*s\n", n, FP_CONSOLE_TEST_LINE_LEN - 6,
             "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
}

static void fp_console_test_log_lines(unsigned first, unsigned count)
{
    char line[FP_CONSOLE_TEST_LINE_LEN + 1];
    for (unsigned n = first; n < (first + count); n++) {
        fp_console_test_line(n, line, sizeof(line));
        fp_console_test_log("%s", line);
    }
}

static size_t fp_console_test_copy(void)
{
    size_t len = 12345;
    TEST_ASSERT_TRUE(fp_console_copy(fp_console_test_out, sizeof(fp_console_test_out), &len));
    fp_console_test_out[len] = '\0';
    return len;
}

static void fp_console_test_start(void)
{
    mock_esp_log_prev_vprintf = fp_console_test_uart_vprintf;
    fp_console_start();
    TEST_ASSERT_NOT_NULL(mock_esp_log_installed_vprintf);
}

void setUp(void)
{
    mock_log_reset();
    mock_heap_caps_reset();
    mock_freertos_semaphore_reset();
    mock_freertos_task_reset();

    free(fp_console_ring);
    fp_console_ring = NULL;
    fp_console_mutex = NULL;
    fp_console_head = 0;
    fp_console_wrapped = false;
    fp_console_prev = NULL;

    fp_console_test_in_isr = pdFALSE;
    fp_console_test_can_yield = pdTRUE;
    fp_console_test_scheduler = taskSCHEDULER_RUNNING;
    fp_console_test_uart[0] = '\0';
    fp_console_test_uart_calls = 0;
    memset(fp_console_test_out, 0, sizeof(fp_console_test_out));
}

void tearDown(void)
{
}


// ── Start ────────────────────────────────────────────────────────────────────────────────

static void test_copy_before_start_fails(void)
{
    size_t len = 7;
    TEST_ASSERT_FALSE(fp_console_copy(fp_console_test_out, sizeof(fp_console_test_out), &len));
    TEST_ASSERT_EQUAL_size_t(0, len);
}

static void test_start_installs_the_hook_once(void)
{
    fp_console_test_start();
    TEST_ASSERT_EQUAL_INT(1, mock_esp_log_set_vprintf_called);
    TEST_ASSERT_TRUE(mock_esp_log_installed_vprintf == fp_console_vprintf);
    TEST_ASSERT_TRUE(fp_console_prev == fp_console_test_uart_vprintf);
    TEST_ASSERT_EQUAL_UINT32(MALLOC_CAP_SPIRAM, mock_heap_caps_last_caps);
    TEST_ASSERT_NOT_NULL(mock_log_find("Keeping the last 32 KB"));

    // A second hook would call itself.
    fp_console_start();
    TEST_ASSERT_EQUAL_INT(1, mock_esp_log_set_vprintf_called);
}

static void test_start_without_psram_leaves_the_log_alone(void)
{
    mock_heap_caps_fail_count = 1;
    fp_console_start();
    TEST_ASSERT_EQUAL_INT(0, mock_esp_log_set_vprintf_called);
    TEST_ASSERT_NOT_NULL(mock_log_find("No PSRAM for the console ring"));
    size_t len = 7;
    TEST_ASSERT_FALSE(fp_console_copy(fp_console_test_out, sizeof(fp_console_test_out), &len));
    TEST_ASSERT_EQUAL_size_t(0, len);
}

static void test_copy_needs_room_for_the_whole_ring(void)
{
    fp_console_test_start();
    size_t len = 7;
    TEST_ASSERT_FALSE(fp_console_copy(fp_console_test_out, FP_CONSOLE_SIZE - 1, &len));
    TEST_ASSERT_EQUAL_size_t(0, len);
    TEST_ASSERT_FALSE(fp_console_copy(NULL, FP_CONSOLE_SIZE, &len));
    TEST_ASSERT_TRUE(fp_console_copy(fp_console_test_out, FP_CONSOLE_SIZE, &len));
}


// ── Capture ──────────────────────────────────────────────────────────────────────────────

static void test_line_goes_to_the_uart_and_to_the_ring(void)
{
    fp_console_test_start();
    int ret = fp_console_test_log("I (%d) fp_acs: Sensor online: %u template slots\n", 1234, 200u);
    TEST_ASSERT_EQUAL_STRING("I (1234) fp_acs: Sensor online: 200 template slots\n", fp_console_test_uart);
    TEST_ASSERT_EQUAL_INT((int)strlen(fp_console_test_uart), ret);

    TEST_ASSERT_EQUAL_size_t(strlen(fp_console_test_uart), fp_console_test_copy());
    TEST_ASSERT_EQUAL_STRING(fp_console_test_uart, fp_console_test_out);
}

static void test_lines_are_kept_oldest_first(void)
{
    fp_console_test_start();
    fp_console_test_log("first\n");
    fp_console_test_log("second %s\n", "line");
    fp_console_test_log("third\n");
    fp_console_test_copy();
    TEST_ASSERT_EQUAL_STRING("first\nsecond line\nthird\n", fp_console_test_out);
}

static void test_without_the_previous_vprintf_the_default_one_prints(void)
{
    mock_esp_log_prev_vprintf = NULL;
    fp_console_start();
    TEST_ASSERT_NULL(fp_console_prev);
    TEST_ASSERT_EQUAL_INT(0, fp_console_test_log("%s", ""));
}

static void test_line_logged_from_an_isr_reaches_only_the_uart(void)
{
    fp_console_test_start();
    fp_console_test_in_isr = pdTRUE;
    fp_console_test_log("from an isr\n");
    TEST_ASSERT_EQUAL_STRING("from an isr\n", fp_console_test_uart);
    TEST_ASSERT_EQUAL_size_t(0, fp_console_test_copy());
}

static void test_line_logged_in_a_critical_section_reaches_only_the_uart(void)
{
    fp_console_test_start();
    fp_console_test_can_yield = pdFALSE;
    fp_console_test_log("in a critical section\n");
    TEST_ASSERT_EQUAL_INT(1, fp_console_test_uart_calls);
    TEST_ASSERT_EQUAL_size_t(0, fp_console_test_copy());
}

static void test_line_logged_before_the_scheduler_runs_reaches_only_the_uart(void)
{
    fp_console_test_start();
    fp_console_test_scheduler = taskSCHEDULER_NOT_STARTED;
    fp_console_test_log("early\n");
    TEST_ASSERT_EQUAL_INT(1, fp_console_test_uart_calls);
    TEST_ASSERT_EQUAL_size_t(0, fp_console_test_copy());
}

static void test_line_is_skipped_when_the_ring_stays_busy(void)
{
    fp_console_test_start();
    mock_xSemaphoreTake_return_value = pdFAIL;
    fp_console_test_log("dropped\n");
    TEST_ASSERT_EQUAL_UINT32(pdMS_TO_TICKS(10), mock_xSemaphoreTake_xTicksToWait);
    TEST_ASSERT_EQUAL_STRING("dropped\n", fp_console_test_uart);

    mock_xSemaphoreTake_return_value = pdPASS;
    fp_console_test_log("kept\n");
    fp_console_test_copy();
    TEST_ASSERT_EQUAL_STRING("kept\n", fp_console_test_out);
}


// ── Long lines ───────────────────────────────────────────────────────────────────────────

static void test_line_of_255_bytes_is_kept_whole(void)
{
    char line[256];
    memset(line, 'a', 254);
    line[254] = '\n';
    line[255] = '\0';
    fp_console_test_start();
    fp_console_test_log("%s", line);
    TEST_ASSERT_EQUAL_size_t(255, fp_console_test_copy());
    TEST_ASSERT_EQUAL_STRING(line, fp_console_test_out);
}

static void test_longer_line_is_cut_to_255_bytes_ending_in_dots(void)
{
    char line[302];
    memset(line, 'b', 300);
    line[300] = '\n';
    line[301] = '\0';
    fp_console_test_start();
    fp_console_test_log("%s", line);
    // The UART still gets all of it.
    TEST_ASSERT_EQUAL_size_t(301, strlen(fp_console_test_uart));

    TEST_ASSERT_EQUAL_size_t(255, fp_console_test_copy());
    for (size_t i = 0; i < 251; i++) {
        TEST_ASSERT_EQUAL_CHAR('b', fp_console_test_out[i]);
    }
    TEST_ASSERT_EQUAL_STRING("...\n", &fp_console_test_out[251]);
}


// ── Wrap ─────────────────────────────────────────────────────────────────────────────────

static void test_wrapped_ring_starts_at_the_first_whole_line(void)
{
    fp_console_test_start();
    fp_console_test_log_lines(0, 400);              // 40000 bytes into 32768

    // The oldest byte is 32 bytes into line 72: line 73 is the first whole one.
    TEST_ASSERT_EQUAL_size_t((400 - 73) * FP_CONSOLE_TEST_LINE_LEN, fp_console_test_copy());
    char line[FP_CONSOLE_TEST_LINE_LEN + 1];
    for (unsigned n = 73; n < 400; n++) {
        fp_console_test_line(n, line, sizeof(line));
        TEST_ASSERT_EQUAL_MEMORY(line, &fp_console_test_out[(n - 73) * FP_CONSOLE_TEST_LINE_LEN],
                                 FP_CONSOLE_TEST_LINE_LEN);
    }
}

static void test_line_written_across_the_end_of_the_ring_reads_back_whole(void)
{
    fp_console_test_start();
    fp_console_test_log_lines(0, 327);              // 32700 bytes: 68 left before the end
    TEST_ASSERT_FALSE(fp_console_wrapped);
    fp_console_test_log_lines(327, 1);
    TEST_ASSERT_TRUE(fp_console_wrapped);
    TEST_ASSERT_EQUAL_size_t(32, fp_console_head);

    // Line 0 lost its first 32 bytes and is skipped; line 327 comes last, in one piece.
    size_t len = fp_console_test_copy();
    TEST_ASSERT_EQUAL_size_t(327 * FP_CONSOLE_TEST_LINE_LEN, len);
    char line[FP_CONSOLE_TEST_LINE_LEN + 1];
    fp_console_test_line(1, line, sizeof(line));
    TEST_ASSERT_EQUAL_MEMORY(line, fp_console_test_out, FP_CONSOLE_TEST_LINE_LEN);
    fp_console_test_line(327, line, sizeof(line));
    TEST_ASSERT_EQUAL_MEMORY(line, &fp_console_test_out[len - FP_CONSOLE_TEST_LINE_LEN], FP_CONSOLE_TEST_LINE_LEN);
}

static void test_wrapped_ring_without_a_newline_is_copied_whole_oldest_first(void)
{
    // 200 fragments of 200 bytes, no newline, each filled with its own letter.
    fp_console_test_start();
    char fragment[201];
    for (unsigned k = 0; k < 200; k++) {
        memset(fragment, 'a' + (int)(k % 26), 200);
        fragment[200] = '\0';
        fp_console_test_log("%s", fragment);
    }
    TEST_ASSERT_EQUAL_size_t(FP_CONSOLE_SIZE, fp_console_test_copy());
    // 40000 bytes written: the oldest kept is byte 7232, inside fragment 36.
    TEST_ASSERT_EQUAL_CHAR('a' + (36 % 26), fp_console_test_out[0]);
    TEST_ASSERT_EQUAL_CHAR('a' + (199 % 26), fp_console_test_out[FP_CONSOLE_SIZE - 1]);
}


int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_copy_before_start_fails);
    RUN_TEST(test_start_installs_the_hook_once);
    RUN_TEST(test_start_without_psram_leaves_the_log_alone);
    RUN_TEST(test_copy_needs_room_for_the_whole_ring);

    RUN_TEST(test_line_goes_to_the_uart_and_to_the_ring);
    RUN_TEST(test_lines_are_kept_oldest_first);
    RUN_TEST(test_without_the_previous_vprintf_the_default_one_prints);
    RUN_TEST(test_line_logged_from_an_isr_reaches_only_the_uart);
    RUN_TEST(test_line_logged_in_a_critical_section_reaches_only_the_uart);
    RUN_TEST(test_line_logged_before_the_scheduler_runs_reaches_only_the_uart);
    RUN_TEST(test_line_is_skipped_when_the_ring_stays_busy);

    RUN_TEST(test_line_of_255_bytes_is_kept_whole);
    RUN_TEST(test_longer_line_is_cut_to_255_bytes_ending_in_dots);

    RUN_TEST(test_wrapped_ring_starts_at_the_first_whole_line);
    RUN_TEST(test_line_written_across_the_end_of_the_ring_reads_back_whole);
    RUN_TEST(test_wrapped_ring_without_a_newline_is_copied_whole_oldest_first);

    return UNITY_END();
}
