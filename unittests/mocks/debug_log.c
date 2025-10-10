#include <stdbool.h>
#include "esp_log.h"

bool debug_log_enabled = true;

bool debug_log_is_enabled(const char* tag)
{
    (void)tag;
    return debug_log_enabled;
}
