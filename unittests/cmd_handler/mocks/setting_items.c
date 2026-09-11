/* setting_items mock for the cmd_handler unit test. Two things reach this module:
 * set_default_settings (recorded, and can be made to fail, which is how the "command execution
 * failed" answer is reached) and the integer reads/writes the factory reset uses to carry the
 * Airzone counters across it.
 *
 * The store holds only those two counters, and models the one property that matters for them:
 * setting_items_set_defaults() rewrites EVERY key back to its default, so it puts both counters
 * back to 0 — which is exactly what the reset path has to work around. */

#include "setting_items.h"

#include <string.h>

int       mock_setting_items_set_defaults_called = 0;
esp_err_t mock_setting_items_set_defaults_result = ESP_OK;

/* Both counters default to DEFAULT_AIRZONE_COUNTER ("0" in main/config.h). */
#define MOCK_COUNTER_DEFAULT   0

static int mock_incl_cnt = MOCK_COUNTER_DEFAULT;
static int mock_set_cnt  = MOCK_COUNTER_DEFAULT;

static int *find_entry(const char *key)
{
    if (strcmp(key, KEY_AIRZONE_INCL_CNT) == 0) {
        return &mock_incl_cnt;
    }
    if (strcmp(key, KEY_AIRZONE_SET_CNT) == 0) {
        return &mock_set_cnt;
    }
    return NULL;
}

void mock_setting_items_reset(void)
{
    mock_setting_items_set_defaults_called = 0;
    mock_setting_items_set_defaults_result = ESP_OK;
    mock_incl_cnt = MOCK_COUNTER_DEFAULT;
    mock_set_cnt  = MOCK_COUNTER_DEFAULT;
}

/* Seed one stored value, so a test can start from a device that has been in service. */
void mock_setting_items_set_int(const char *key, int value)
{
    int *entry = find_entry(key);
    if (entry != NULL) {
        *entry = value;
    }
}

esp_err_t setting_items_set_defaults(bool only_uninitialized)
{
    (void)only_uninitialized;
    mock_setting_items_set_defaults_called++;
    if (mock_setting_items_set_defaults_result != ESP_OK) {
        return mock_setting_items_set_defaults_result;
    }
    mock_incl_cnt = MOCK_COUNTER_DEFAULT;
    mock_set_cnt  = MOCK_COUNTER_DEFAULT;
    return ESP_OK;
}

int setting_items_read_int(const char *key)
{
    const int *entry = find_entry(key);
    return (entry != NULL) ? *entry : 0;
}

esp_err_t setting_items_save_int(const char *key, int value)
{
    int *entry = find_entry(key);
    if (entry == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    *entry = value;
    return ESP_OK;
}
