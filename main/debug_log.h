#pragma once

#define DEBUG_LOG_ENABLE        1   // Global debug logs enable

void debug_log_init(void);
bool debug_log_is_enabled(const char* tag);
