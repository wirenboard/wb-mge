#include "trace.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char   trace_buf[TRACE_MAX_LEN];
static size_t trace_len;

void trace_add(const char *fmt, ...)
{
    if ((trace_len > 0) && (trace_len < (sizeof(trace_buf) - 1))) {
        trace_buf[trace_len++] = ' ';
        trace_buf[trace_len] = '\0';
    }
    if (trace_len >= (sizeof(trace_buf) - 1)) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(trace_buf + trace_len, sizeof(trace_buf) - trace_len, fmt, args);
    va_end(args);
    if (written > 0) {
        trace_len += (size_t)written;
        if (trace_len >= sizeof(trace_buf)) {
            trace_len = sizeof(trace_buf) - 1;
        }
    }
}

const char *trace_get(void)
{
    return trace_buf;
}

void trace_reset(void)
{
    trace_buf[0] = '\0';
    trace_len = 0;
}
