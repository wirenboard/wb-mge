#pragma once

/*
 * A space-separated trace of what the code under test did to its fakes, in order: "W7.2=1 D1500
 * W7.2=0". A whole sequence then reads and compares as one string, and a failing comparison prints
 * both sequences side by side.
 */

#include <stddef.h>

#define TRACE_MAX_LEN               4096

void        trace_add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
const char *trace_get(void);
void        trace_reset(void);
