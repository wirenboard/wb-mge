#pragma once

#include <stdbool.h>

// Control side of the setting_items mock: a plain key/value store standing in for NVS.
// Keys that were never set read back as "missing", which is what setting_items_read()
// reports with ESP_FAIL on the target.

void mock_setting_items_reset(void);
void mock_setting_items_set(const char *key, const char *value);
void mock_setting_items_set_bool(const char *key, bool value);
