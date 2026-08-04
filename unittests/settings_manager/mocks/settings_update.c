// Stub for settings_update — settings_manager unit tests only observe that it is called and,
// for the cache-overlay warning, what it reports back through its out-parameter.
#include "settings_update.h"

int mock_settings_update_call_count = 0;

// What settings_update_with_status() writes into *cache_apply_err: the result of the synchronous
// runtime cache-overlay apply. ESP_OK by default (the overlay already matched NVS, the common
// case); set it to an error to exercise the "saved but not applied" response warning.
esp_err_t mock_settings_update_cache_apply_result = ESP_OK;

void mock_settings_update_reset(void)
{
    mock_settings_update_call_count = 0;
    mock_settings_update_cache_apply_result = ESP_OK;
}

esp_err_t settings_update_with_status(esp_err_t *cache_apply_err)
{
    mock_settings_update_call_count++;
    if (cache_apply_err != NULL) {
        *cache_apply_err = mock_settings_update_cache_apply_result;
    }
    return ESP_OK;
}

// Mirrors production: the no-status entry point is the same call with nowhere to report to, so
// both spellings bump the same counter and a test cannot pass by watching only one of them.
esp_err_t settings_update(void)
{
    return settings_update_with_status(NULL);
}
