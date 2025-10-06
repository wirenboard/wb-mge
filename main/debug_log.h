#pragma once

#include <stdbool.h>

void debug_log_init(void);
bool debug_log_is_enabled(const char* tag);
