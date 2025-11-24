#pragma once

#include <stdbool.h>

// Global debug logs enable
#if CONFIG_LOG_MAXIMUM_LEVEL_DEBUG || (CONFIG_LOG_MAXIMUM_LEVEL >= 4)
    #define DEBUG_LOG_ENABLE        1
#else
    #define DEBUG_LOG_ENABLE        0
#endif

void debug_log_init(void);
bool debug_log_is_enabled(const char* tag);
