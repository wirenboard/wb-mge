// Mock for setting_items used by the settings_update unit tests.
// Nothing settings_update reads through this interface drives a test today, so every read
// answers with a benign default.

#include "setting_items.h"

bool setting_items_read_bool(const char *key)
{
    (void)key;
    return false;
}

int setting_items_read_int(const char *key)
{
    (void)key;
    return 0;
}
