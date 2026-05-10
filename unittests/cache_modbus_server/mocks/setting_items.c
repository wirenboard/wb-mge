#include "setting_items.h"
#include <stdbool.h>

/* ---- Mock state ---------------------------------------------------------- */

static int mock_cache_value_timeout_s = 0;

/* ---- Mock implementations ------------------------------------------------ */

int setting_items_read_int(const char *key)
{
    (void)key;
    return mock_cache_value_timeout_s;
}

bool setting_items_read_bool(const char *key)
{
    (void)key;
    return false;
}

esp_err_t setting_items_read(const char *key, char *value)
{
    (void)key; (void)value;
    return 0; /* ESP_OK */
}

esp_err_t setting_items_save(const char *key, const char *value)
{
    (void)key; (void)value;
    return 0; /* ESP_OK */
}

esp_err_t setting_items_init(void)
{
    return 0; /* ESP_OK */
}

esp_err_t setting_items_init_with_storage(const setting_storage_iface_t *storage_iface)
{
    (void)storage_iface;
    return 0; /* ESP_OK */
}

esp_err_t setting_items_save_bool(const char *key, bool value)
{
    (void)key; (void)value;
    return 0; /* ESP_OK */
}

esp_err_t setting_items_save_int(const char *key, int value)
{
    (void)key; (void)value;
    return 0; /* ESP_OK */
}

esp_err_t setting_items_set_defaults(bool only_uninitialized)
{
    (void)only_uninitialized;
    return 0; /* ESP_OK */
}

size_t setting_items_get_count(void)
{
    return 0;
}

const char *setting_items_get_key_at(size_t index)
{
    (void)index;
    return NULL;
}

const char *setting_items_get_default_value(const char *key)
{
    (void)key;
    return NULL;
}

setting_item_type_t setting_items_get_type(const char *key)
{
    (void)key;
    return SETTING_ITEM_TYPE_INVALID;
}

const char *setting_items_type_to_string(setting_item_type_t type)
{
    (void)type;
    return NULL;
}

/* ---- Test helpers -------------------------------------------------------- */

void mock_setting_items_set_timeout(int timeout_s)
{
    mock_cache_value_timeout_s = timeout_s;
}

void mock_setting_items_reset(void)
{
    mock_cache_value_timeout_s = 0;
}
