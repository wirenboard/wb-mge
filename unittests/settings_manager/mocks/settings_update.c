// Stub for settings_update — settings_manager unit tests only observe that it is called.
#include "settings_update.h"

int mock_settings_update_call_count = 0;

void mock_settings_update_reset(void)
{
    mock_settings_update_call_count = 0;
}

esp_err_t settings_update(void)
{
    mock_settings_update_call_count++;
    return ESP_OK;
}
