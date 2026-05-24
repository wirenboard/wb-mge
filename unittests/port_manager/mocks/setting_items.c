#include "setting_items.h"
#include "bridge.h"  /* for BRIDGES_COUNT */
#include <string.h>
#include <stdlib.h>

/* Stored mock values for port modes (index 0 = port_mode_1, index 1 = port_mode_2) */
static char mock_port_mode[BRIDGES_COUNT][SETTING_ITEM_MAX_STR_LEN];
bool mock_cache_server_enabled = false;
int mock_cache_port = 0;

/* Count how many times save was called (for diagnostics) */
int mock_setting_items_save_called = 0;

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
esp_err_t setting_items_save_bool(const char *key, bool value)        { (void)key; (void)value; return ESP_OK; }
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
    mock_setting_items_save_called = 0;
}
