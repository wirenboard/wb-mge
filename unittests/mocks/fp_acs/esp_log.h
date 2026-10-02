#pragma once

/*
 * esp_log.h for the fingerprint access control suites (fp_lock, fp_log, fp_console, fp_acs, r503,
 * tg_bot, wb_device). It shadows unittests/mocks/esp_log.h - this directory comes first on their
 * include path - and differs from it in one way: every ESP_LOGx line is also captured, formatted,
 * into a ring the tests can search. Several properties of these modules are visible only in the
 * log: fp_lock and tg_bot log a change of their online state and nothing else, fp_log counts the
 * records it skipped in one warning.
 *
 * Output follows the same policy as the shared mock: silent unless UT_LOG_LEVEL (1..5) is set, and
 * then on stderr, never interleaved with Unity's stdout.
 *
 * It also carries the vprintf hook fp_console installs (vprintf_like_t, esp_log_set_vprintf()).
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum {
    ESP_LOG_NONE    = 0,
    ESP_LOG_ERROR   = 1,
    ESP_LOG_WARN    = 2,
    ESP_LOG_INFO    = 3,
    ESP_LOG_DEBUG   = 4,
    ESP_LOG_VERBOSE = 5,
    ESP_LOG_MAX     = 6,
} esp_log_level_t;

typedef int (*vprintf_like_t)(const char *, va_list);

#define MOCK_LOG_LINES_MAX      128
#define MOCK_LOG_LINE_LEN       320

void mock_log_record(esp_log_level_t level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

// Forgets every captured line.
void        mock_log_reset(void);
// Lines captured since the last reset (the ring keeps the last MOCK_LOG_LINES_MAX of them).
size_t      mock_log_count(void);
// Captured lines that contain needle, at any level.
size_t      mock_log_count_matching(const char *needle);
// Captured lines of this level that contain needle.
size_t      mock_log_count_level(esp_log_level_t level, const char *needle);
// The most recent captured line that contains needle, or NULL.
const char *mock_log_find(const char *needle);

#define ESP_LOGE(tag, format, ...) mock_log_record(ESP_LOG_ERROR,   tag, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) mock_log_record(ESP_LOG_WARN,    tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) mock_log_record(ESP_LOG_INFO,    tag, format, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) mock_log_record(ESP_LOG_DEBUG,   tag, format, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) mock_log_record(ESP_LOG_VERBOSE, tag, format, ##__VA_ARGS__)

// esp_log_set_vprintf(): records the function installed and returns mock_esp_log_prev_vprintf as
// the one it replaced.
vprintf_like_t esp_log_set_vprintf(vprintf_like_t func);

extern int            mock_esp_log_set_vprintf_called;
extern vprintf_like_t mock_esp_log_installed_vprintf;
extern vprintf_like_t mock_esp_log_prev_vprintf;
