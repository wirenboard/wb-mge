#include "nv_storage.h"

// setting_items.c is linked into this suite only for its settings table, and the table
// is read through an injected setting_storage_iface_t. These stubs exist purely to
// satisfy the nvs_storage_iface initialiser at link time and are never called.
esp_err_t nvs_init(void) { return ESP_OK; }
bool nvs_has_key(const char *key) { (void)key; return false; }
esp_err_t nvs_write_str(const char *key, const char *value) { (void)key; (void)value; return ESP_FAIL; }
esp_err_t nvs_read_str(const char *key, char *value) { (void)key; (void)value; return ESP_ERR_NOT_FOUND; }
esp_err_t nvs_erase_str(const char *key) { (void)key; return ESP_OK; }
