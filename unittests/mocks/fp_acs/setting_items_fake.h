#pragma once

/*
 * A settings store for the fingerprint access control suites: setting_items_read() and
 * setting_items_read_int() served from a small key/value table the test fills.
 */

#include "esp_err.h"

// Sets (or replaces) the value of a key, as a string; setting_items_read_int() parses it.
void mock_settings_set(const char *key, const char *value);
// Makes the key read as missing: setting_items_read() returns ESP_ERR_NOT_FOUND.
void mock_settings_unset(const char *key);
void mock_settings_reset(void);
// Calls to setting_items_read() and setting_items_read_int() since the last reset.
extern int mock_settings_reads;
// What setting_items_save() returns, ESP_OK after a reset; on ESP_OK it stores the value as
// mock_settings_set() does.
extern esp_err_t mock_settings_save_result;
