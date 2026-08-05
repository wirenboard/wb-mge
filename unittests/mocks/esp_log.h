#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ESP_LOG_NONE    = 0,    /*!< No log output */
    ESP_LOG_ERROR   = 1,    /*!< Critical errors, software module can not recover on its own */
    ESP_LOG_WARN    = 2,    /*!< Error conditions from which recovery measures have been taken */
    ESP_LOG_INFO    = 3,    /*!< Information messages which describe normal flow of events */
    ESP_LOG_DEBUG   = 4,    /*!< Extra information which is not necessary for normal use (values, pointers, sizes, etc). */
    ESP_LOG_VERBOSE = 5,    /*!< Bigger chunks of debugging information, or frequent messages which can potentially flood the output. */
    ESP_LOG_MAX     = 6,    /*!< Number of levels supported */
} esp_log_level_t;

/*
 * Unit-test logging policy.
 *
 * Production code under test calls ESP_LOGx() freely, including on deliberately
 * exercised error paths. Printing that to stdout interleaves it with Unity's
 * PASS/FAIL output and makes the test result impossible to parse reliably
 * ("[ERROR] ..." lines look like failures when they are not).
 *
 * Therefore this mock SILENCES all ESP_LOGx output by default. The Unity result
 * lines and the test-authored CONS_COLOR narration are then the only thing on
 * stdout, so `make` output is cleanly machine-readable and the process exit code
 * (the number of Unity failures) is the source of truth.
 *
 * To see production logs while debugging a specific test, set the verbosity via
 * the UT_LOG_LEVEL environment variable (1=ERROR .. 5=VERBOSE), e.g.:
 *     UT_LOG_LEVEL=4 make
 * When enabled, logs go to stderr so they never interleave with Unity's stdout.
 */
static inline int ut_log_level(void)
{
    static int level = -1;
    if (level < 0) {
        const char *env = getenv("UT_LOG_LEVEL");
        level = (env != NULL) ? atoi(env) : (int)ESP_LOG_NONE;  /* silent by default */
        if (level < 0) {
            level = (int)ESP_LOG_NONE;  /* clamp junk/negative so the cache stays valid */
        }
    }
    return level;
}

#define UT_LOG_AT(lvl, prefix, tag, format, ...) \
    do { \
        if ((int)(lvl) <= ut_log_level()) { \
            fprintf(stderr, "[" prefix "] %s: " format "\n", tag, ##__VA_ARGS__); \
        } \
    } while (0)

#define ESP_LOGE(tag, format, ...) UT_LOG_AT(ESP_LOG_ERROR,   "ERROR",   tag, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) UT_LOG_AT(ESP_LOG_WARN,    "WARN",    tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) UT_LOG_AT(ESP_LOG_INFO,    "INFO",    tag, format, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) UT_LOG_AT(ESP_LOG_DEBUG,   "DEBUG",   tag, format, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) UT_LOG_AT(ESP_LOG_VERBOSE, "VERBOSE", tag, format, ##__VA_ARGS__)

#define ESP_LOG_BUFFER_HEX_LEVEL(tag, buffer, buff_len, level) \
    do { \
        if ((int)(level) <= ut_log_level()) { \
            fprintf(stderr, "[BUFFER_HEX] %s: ", tag); \
            for (size_t _i = 0; _i < (buff_len); _i++) { \
                fprintf(stderr, "%02x ", ((const uint8_t*)(buffer))[_i]); \
            } \
            fprintf(stderr, "\n"); \
        } \
    } while (0)

#define ESP_LOG_LEVEL(level, tag, format, ...) \
    do { \
        if ((int)(level) <= ut_log_level()) { \
            const char *_p = (level) == ESP_LOG_ERROR ? "ERROR" : \
                             (level) == ESP_LOG_WARN  ? "WARN"  : \
                             (level) == ESP_LOG_INFO  ? "INFO"  : "DEBUG"; \
            fprintf(stderr, "[%s] %s: " format "\n", _p, tag, ##__VA_ARGS__); \
        } \
    } while (0)
