#include "esp_log.h"

#include <string.h>

static char   mock_log_lines[MOCK_LOG_LINES_MAX][MOCK_LOG_LINE_LEN];
static size_t mock_log_total;

int            mock_esp_log_set_vprintf_called = 0;
vprintf_like_t mock_esp_log_installed_vprintf = NULL;
vprintf_like_t mock_esp_log_prev_vprintf = NULL;

static const char *const mock_log_prefixes[ESP_LOG_MAX] = {"N", "E", "W", "I", "D", "V"};

// UT_LOG_LEVEL as the shared mock reads it: silent unless set.
static int mock_log_print_level(void)
{
    static int level = -1;
    if (level < 0) {
        const char *env = getenv("UT_LOG_LEVEL");
        level = (env != NULL) ? atoi(env) : (int)ESP_LOG_NONE;
        if (level < 0) {
            level = (int)ESP_LOG_NONE;
        }
    }
    return level;
}

void mock_log_record(esp_log_level_t level, const char *tag, const char *fmt, ...)
{
    char *line = mock_log_lines[mock_log_total % MOCK_LOG_LINES_MAX];
    int used = snprintf(line, MOCK_LOG_LINE_LEN, "%s %s: ", mock_log_prefixes[level], tag);
    if ((used > 0) && ((size_t)used < MOCK_LOG_LINE_LEN)) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(line + used, MOCK_LOG_LINE_LEN - (size_t)used, fmt, args);
        va_end(args);
    }
    mock_log_total++;

    if ((int)level <= mock_log_print_level()) {
        fprintf(stderr, "%s\n", line);
    }
}

void mock_log_reset(void)
{
    memset(mock_log_lines, 0, sizeof(mock_log_lines));
    mock_log_total = 0;
    mock_esp_log_set_vprintf_called = 0;
    mock_esp_log_installed_vprintf = NULL;
    mock_esp_log_prev_vprintf = NULL;
}

size_t mock_log_count(void)
{
    return mock_log_total;
}

static size_t mock_log_kept(void)
{
    return (mock_log_total < MOCK_LOG_LINES_MAX) ? mock_log_total : MOCK_LOG_LINES_MAX;
}

size_t mock_log_count_matching(const char *needle)
{
    size_t count = 0;
    for (size_t i = 0; i < mock_log_kept(); i++) {
        if (strstr(mock_log_lines[i], needle) != NULL) {
            count++;
        }
    }
    return count;
}

size_t mock_log_count_level(esp_log_level_t level, const char *needle)
{
    size_t count = 0;
    for (size_t i = 0; i < mock_log_kept(); i++) {
        const char *line = mock_log_lines[i];
        if ((line[0] == mock_log_prefixes[level][0]) && (strstr(line, needle) != NULL)) {
            count++;
        }
    }
    return count;
}

const char *mock_log_find(const char *needle)
{
    // Newest first: the ring slot of the last line, backwards.
    for (size_t n = 0; n < mock_log_kept(); n++) {
        size_t index = (mock_log_total - 1 - n) % MOCK_LOG_LINES_MAX;
        if (strstr(mock_log_lines[index], needle) != NULL) {
            return mock_log_lines[index];
        }
    }
    return NULL;
}

vprintf_like_t esp_log_set_vprintf(vprintf_like_t func)
{
    mock_esp_log_set_vprintf_called++;
    mock_esp_log_installed_vprintf = func;
    return mock_esp_log_prev_vprintf;
}
