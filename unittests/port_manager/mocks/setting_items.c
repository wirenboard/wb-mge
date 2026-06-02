#include "setting_items.h"
#include "bridge.h"  /* for BRIDGES_COUNT */
#include <string.h>
#include <stdlib.h>

/* Stored mock values for port modes (index 0 = port_mode_1, index 1 = port_mode_2) */
static char mock_port_mode[BRIDGES_COUNT][SETTING_ITEM_MAX_STR_LEN];
bool mock_cache_server_enabled = false;
int mock_cache_port = 0;

/* Stored mock values for per-port cache overlay (cache_en_1 / cache_en_2) */
bool mock_cache_en[BRIDGES_COUNT] = {false, false};

/* Count how many times save was called (for diagnostics) */
int mock_setting_items_save_called = 0;
int mock_setting_items_save_bool_called = 0;

/* When set, the corresponding save returns an error (persist-6 tests). The
 * stored value is NOT updated, mimicking an NVS write failure. */
bool mock_setting_items_save_should_fail = false;
bool mock_setting_items_save_bool_should_fail = false;

static int index_from_port_mode_key(const char *key)
{
    if (strcmp(key, KEY_PORT_MODE1) == 0) {
        return 0;
    }
    if (strcmp(key, KEY_PORT_MODE2) == 0) {
        return 1;
    }
    return -1;
}

static int index_from_cache_en_key(const char *key)
{
    if (strcmp(key, KEY_CACHE_EN_1) == 0) {
        return 0;
    }
    if (strcmp(key, KEY_CACHE_EN_2) == 0) {
        return 1;
    }
    return -1;
}

/* Test helper: directly set the stored port-mode string (e.g. legacy values). */
void mock_setting_items_set_port_mode(unsigned index, const char *value)
{
    if (index >= BRIDGES_COUNT) return;
    strncpy(mock_port_mode[index], value, SETTING_ITEM_MAX_STR_LEN - 1);
    mock_port_mode[index][SETTING_ITEM_MAX_STR_LEN - 1] = '\0';
}

/* Test helper: read the stored port-mode string. */
const char *mock_setting_items_get_port_mode(unsigned index)
{
    if (index >= BRIDGES_COUNT) return "";
    return mock_port_mode[index];
}

esp_err_t setting_items_read(const char *key, char *value)
{
    int idx = index_from_port_mode_key(key);
    if (idx >= 0) {
        strncpy(value, mock_port_mode[idx], SETTING_ITEM_MAX_STR_LEN);
        return ESP_OK;
    }
    /* Unknown key — return a sensible default rather than failing */
    value[0] = '\0';
    return ESP_OK;
}

esp_err_t setting_items_save(const char *key, const char *value)
{
    mock_setting_items_save_called++;
    if (mock_setting_items_save_should_fail) {
        return ESP_FAIL;  /* NVS write failed: stored value left unchanged */
    }
    int idx = index_from_port_mode_key(key);
    if (idx >= 0) {
        strncpy(mock_port_mode[idx], value, SETTING_ITEM_MAX_STR_LEN - 1);
        mock_port_mode[idx][SETTING_ITEM_MAX_STR_LEN - 1] = '\0';
    }
    return ESP_OK;
}

bool setting_items_read_bool(const char *key)
{
    if (strcmp(key, KEY_CACHE_MODBUS_SERVER_ENABLED) == 0) {
        return mock_cache_server_enabled;
    }
    int ci = index_from_cache_en_key(key);
    if (ci >= 0) {
        return mock_cache_en[ci];
    }
    return false;
}

int setting_items_read_int(const char *key)
{
    if (strcmp(key, KEY_CACHE_MODBUS_PORT) == 0) {
        return mock_cache_port;
    }
    return 0;
}

/* Stubs for functions not used by port_manager directly */
esp_err_t setting_items_init(void)                                    { return ESP_OK; }
esp_err_t setting_items_init_with_storage(const setting_storage_iface_t *s) { (void)s; return ESP_OK; }
esp_err_t setting_items_save_bool(const char *key, bool value)
{
    mock_setting_items_save_bool_called++;
    if (mock_setting_items_save_bool_should_fail) {
        return ESP_FAIL;  /* NVS write failed: stored value left unchanged */
    }
    int ci = index_from_cache_en_key(key);
    if (ci >= 0) {
        mock_cache_en[ci] = value;
    }
    return ESP_OK;
}
esp_err_t setting_items_save_int(const char *key, int value)          { (void)key; (void)value; return ESP_OK; }
esp_err_t setting_items_set_defaults(bool only_uninitialized)         { (void)only_uninitialized; return ESP_OK; }
size_t setting_items_get_count(void)                                  { return 0; }
const char *setting_items_get_key_at(size_t index)                    { (void)index; return NULL; }
const char *setting_items_get_default_value(const char *key)          { (void)key; return NULL; }
setting_item_type_t setting_items_get_type(const char *key)           { (void)key; return SETTING_ITEM_TYPE_INVALID; }
const char *setting_items_type_to_string(setting_item_type_t type)    { (void)type; return "unknown"; }
esp_err_t setting_items_validate(const char *key, const char *value)  { (void)key; (void)value; return ESP_OK; }

void mock_setting_items_reset(void)
{
    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        /* Default: disabled */
        strncpy(mock_port_mode[i], PORT_MODE_DISABLED_STR, SETTING_ITEM_MAX_STR_LEN - 1);
        mock_port_mode[i][SETTING_ITEM_MAX_STR_LEN - 1] = '\0';
    }
    mock_cache_server_enabled = false;
    mock_cache_port = 0;
    mock_cache_en[0] = false;
    mock_cache_en[1] = false;
    mock_setting_items_save_called = 0;
    mock_setting_items_save_bool_called = 0;
    mock_setting_items_save_should_fail = false;
    mock_setting_items_save_bool_should_fail = false;
}
