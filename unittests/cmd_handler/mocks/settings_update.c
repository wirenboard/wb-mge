/* Stub for settings_update — the cmd_handler tests only observe that it is called.
 *
 * settings_factory_reset() is a recording stub on purpose: what the reset has to get right (the
 * Airzone counters across it, the values-then-counter order) is tested against the real function
 * in the settings_update suite. A faithful copy of that logic here would only ever test the copy;
 * all cmd_execute() still decides is that it calls the reset once, in front of settings_update(),
 * and reports a failure instead of applying anything. */

#include "settings_update.h"

int mock_settings_update_call_count = 0;

int       mock_settings_factory_reset_call_count = 0;
esp_err_t mock_settings_factory_reset_result = ESP_OK;

/* How many resets had happened when settings_update() was called, so a test can pin the order
 * rather than just the two calls. -1 = settings_update() has not been called. */
int mock_settings_factory_resets_at_update = -1;

void mock_settings_update_reset(void)
{
    mock_settings_update_call_count = 0;
    mock_settings_factory_reset_call_count = 0;
    mock_settings_factory_reset_result = ESP_OK;
    mock_settings_factory_resets_at_update = -1;
}

esp_err_t settings_update_with_status(esp_err_t *cache_apply_err)
{
    mock_settings_update_call_count++;
    mock_settings_factory_resets_at_update = mock_settings_factory_reset_call_count;
    if (cache_apply_err != NULL) {
        *cache_apply_err = ESP_OK;
    }
    return ESP_OK;
}

esp_err_t settings_factory_reset(void)
{
    mock_settings_factory_reset_call_count++;
    return mock_settings_factory_reset_result;
}

esp_err_t settings_update(void)
{
    return settings_update_with_status(NULL);
}
