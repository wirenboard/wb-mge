#pragma once

/* Test-side controls of the settings mock. The production interface itself stays in the
 * REAL main/setting_items.h, which the mock implements — the keys and the value-length cap
 * the engine uses have to be the firmware's, not a copy. */

extern int mock_setting_items_save_called;

void mock_setting_items_set(const char *key, const char *value);
const char *mock_setting_items_get(const char *key);
void mock_setting_items_reset(void);
