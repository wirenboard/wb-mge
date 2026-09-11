// Mock for setting_items used by the settings_update unit tests.
// Two things reach this module: settings_update() reads the persisted cache Modbus server
// settings, and settings_factory_reset() resets every stored key and then writes the two Airzone
// counters back over the defaults.
//
// The cache settings stay directly settable values instead of a full key/value store; the two
// counters are a tiny store, because the reset has to read them, see them wiped and write them
// back. What the store models is the one property that makes the reset delicate:
// setting_items_set_defaults() rewrites EVERY key back to its default — both counters to 0 and
// the cache settings to the values main/config.h ships.

#include "setting_items.h"

#include <string.h>

// Defaults as main/config.h spells them: DEFAULT_AIRZONE_COUNTER "0",
// DEFAULT_CACHE_MODBUS_SERVER_ENABLED "true", DEFAULT_CACHE_MODBUS_PORT "504".
#define MOCK_COUNTER_DEFAULT            0
#define MOCK_CACHE_ENABLED_DEFAULT      true
#define MOCK_CACHE_PORT_DEFAULT         504

bool mock_setting_items_cache_server_enabled = false;
int  mock_setting_items_cache_port = 0;

int       mock_setting_items_set_defaults_called = 0;
esp_err_t mock_setting_items_set_defaults_result = ESP_OK;

/* Key whose setting_items_save_int() fails, to model a counter that cannot be written back over
 * the freshly restored defaults. NULL = every save succeeds. */
const char *mock_setting_items_save_int_fail_key = NULL;

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

// The baseline is a device that has been in service, not a factory-fresh one: the cache settings
// start where the existing cache tests expect them (server off, no port), and only
// setting_items_set_defaults() below puts the shipped defaults in.
void mock_setting_items_reset(void)
{
    mock_setting_items_cache_server_enabled = false;
    mock_setting_items_cache_port = 0;
    mock_setting_items_set_defaults_called = 0;
    mock_setting_items_set_defaults_result = ESP_OK;
    mock_setting_items_save_int_fail_key = NULL;
    mock_incl_cnt = MOCK_COUNTER_DEFAULT;
    mock_set_cnt  = MOCK_COUNTER_DEFAULT;
}

/* Seed one stored counter, so a test can start from a device that has been in service. */
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
    mock_setting_items_cache_server_enabled = MOCK_CACHE_ENABLED_DEFAULT;
    mock_setting_items_cache_port = MOCK_CACHE_PORT_DEFAULT;
    return ESP_OK;
}

bool setting_items_read_bool(const char *key)
{
    if (key && (strcmp(key, KEY_CACHE_MODBUS_SERVER_ENABLED) == 0)) {
        return mock_setting_items_cache_server_enabled;
    }
    return false;
}

int setting_items_read_int(const char *key)
{
    if (key == NULL) {
        return 0;
    }
    if (strcmp(key, KEY_CACHE_MODBUS_PORT) == 0) {
        return mock_setting_items_cache_port;
    }
    const int *entry = find_entry(key);
    return (entry != NULL) ? *entry : 0;
}

esp_err_t setting_items_save_int(const char *key, int value)
{
    if (key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((mock_setting_items_save_int_fail_key != NULL) &&
        (strcmp(key, mock_setting_items_save_int_fail_key) == 0))
    {
        return ESP_FAIL;
    }
    int *entry = find_entry(key);
    if (entry == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    *entry = value;
    return ESP_OK;
}
