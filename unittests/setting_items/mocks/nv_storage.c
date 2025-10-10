#include "nv_storage.h"

bool nvs_has_key(const char *key)
{
    (void)key;
    return false;
}

esp_err_t nvs_write_str(const char *key, const char *value)
{
    (void)key;
    (void)value;
    return ESP_FAIL;
}

esp_err_t nvs_read_str(const char *key, char *value)
{
    (void)key;
    (void)value;
    return ESP_ERR_NOT_FOUND;
}
