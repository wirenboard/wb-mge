// Mock for setting_items used by the settings_update unit tests.
// settings_update only reads the persisted cache Modbus server settings, so the mock exposes them
// as directly settable values instead of a full key/value store.

#include "setting_items.h"

#include <string.h>

bool mock_setting_items_cache_server_enabled = false;
int  mock_setting_items_cache_port = 0;

void mock_setting_items_reset(void)
{
    mock_setting_items_cache_server_enabled = false;
    mock_setting_items_cache_port = 0;
}

bool setting_items_read_bool(const char *key)
{
    if (key && (strcmp(key, KEY_CACHE_MODBUS_SERVER_ENABLED) == 0)) {
        return mock_setting_items_cache_server_enabled;
    }
    return false;
}

int setting_items_read_int(const char *key)
{
    if (key && (strcmp(key, KEY_CACHE_MODBUS_PORT) == 0)) {
        return mock_setting_items_cache_port;
    }
    return 0;
}
