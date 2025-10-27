#include "config.h"
#include "setting_items.h"

#include <string.h>

static bool mock_settings[MAX_FUNCTION_CALLS];
static const char* mock_setting_keys[MAX_FUNCTION_CALLS];
static int mock_settings_count = 0;

int mock_setting_items_read_bool_called = 0;
char mock_setting_items_read_bool_keys[MAX_FUNCTION_CALLS][64];

bool setting_items_read_bool(const char *key)
{
    if (mock_setting_items_read_bool_called < MAX_FUNCTION_CALLS) {
        strncpy(mock_setting_items_read_bool_keys[mock_setting_items_read_bool_called],
                key,
                sizeof(mock_setting_items_read_bool_keys[0]) - 1);
        mock_setting_items_read_bool_keys[mock_setting_items_read_bool_called][63] = '\0';
    }
    mock_setting_items_read_bool_called++;

    for (int i = 0; i < mock_settings_count; i++) {
        if (strcmp(mock_setting_keys[i], key) == 0) {
            return mock_settings[i];
        }
    }

    return false;
}

void mock_setting_items_set_bool(const char *key, bool value)
{
    for (int i = 0; i < mock_settings_count; i++) {
        if (strcmp(mock_setting_keys[i], key) == 0) {
            mock_settings[i] = value;
            return;
        }
    }

    if (mock_settings_count < MAX_FUNCTION_CALLS) {
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
