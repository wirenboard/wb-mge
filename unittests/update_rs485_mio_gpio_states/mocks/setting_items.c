#include "setting_items.h"
#include <string.h>
#include <stdbool.h>

// Storage for mock setting values
static bool mock_settings[10];
static const char* mock_setting_keys[10];
static int mock_settings_count = 0;

// Mock tracking for setting_items_read_bool
int mock_setting_items_read_bool_called = 0;
char mock_setting_items_read_bool_keys[10][64];

bool setting_items_read_bool(const char *key)
{
    if (mock_setting_items_read_bool_called < 10) {
        strncpy(mock_setting_items_read_bool_keys[mock_setting_items_read_bool_called],
                key,
                sizeof(mock_setting_items_read_bool_keys[0]) - 1);
        mock_setting_items_read_bool_keys[mock_setting_items_read_bool_called][63] = '\0';
    }
    mock_setting_items_read_bool_called++;

    // Look up the value in our mock settings
    for (int i = 0; i < mock_settings_count; i++) {
        if (strcmp(mock_setting_keys[i], key) == 0) {
            return mock_settings[i];
        }
    }

    return false;
}

void mock_setting_items_set_bool(const char *key, bool value)
{
    // Check if key already exists
    for (int i = 0; i < mock_settings_count; i++) {
        if (strcmp(mock_setting_keys[i], key) == 0) {
            mock_settings[i] = value;
            return;
        }
    }

    // Add new setting
    if (mock_settings_count < 10) {
        mock_setting_keys[mock_settings_count] = key;
        mock_settings[mock_settings_count] = value;
        mock_settings_count++;
    }
}

void mock_setting_items_reset(void)
{
    mock_settings_count = 0;
    mock_setting_items_read_bool_called = 0;
    memset(mock_settings, 0, sizeof(mock_settings));
    memset(mock_setting_items_read_bool_keys, 0, sizeof(mock_setting_items_read_bool_keys));
}
