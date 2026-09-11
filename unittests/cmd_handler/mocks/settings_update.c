/* Stub for settings_update — the cmd_handler tests only observe that it is called. */

#include "settings_update.h"

int mock_settings_update_call_count = 0;

void mock_settings_update_reset(void)
{
    mock_settings_update_call_count = 0;
}

esp_err_t settings_update_with_status(esp_err_t *cache_apply_err)
{
    mock_settings_update_call_count++;
    if (cache_apply_err != NULL) {
        *cache_apply_err = ESP_OK;
    }
    return ESP_OK;
}

esp_err_t settings_update(void)
{
    return settings_update_with_status(NULL);
}
