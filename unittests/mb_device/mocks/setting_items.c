#include "setting_items.h"
#include <stdbool.h>

/* ---- Mock state ---------------------------------------------------------- */

/* Returned by setting_items_read_int() — used by mb_device.c for the cache
 * value timeout register (KEY_CACHE_VALUE_TIMEOUT_S). */
static int mock_cache_value_timeout_s = 0;

/* ---- Mock implementations ------------------------------------------------ */

int setting_items_read_int(const char *key)
{
    (void)key;
    return mock_cache_value_timeout_s;
}

/* ---- Test helpers -------------------------------------------------------- */

void mock_setting_items_set_timeout(int timeout_s)
{
    mock_cache_value_timeout_s = timeout_s;
}

void mock_setting_items_reset(void)
{
    mock_cache_value_timeout_s = 0;
}
