#pragma once
#include <stdint.h>

/* Stub for esp_timer_get_time — returns 0 in unit test environment */
static inline int64_t esp_timer_get_time(void) { return 0; }
