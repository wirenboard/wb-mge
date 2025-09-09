#include "nv_storage.h"

// Minimal stub implementations for linking - never called in unit tests
// Functions are referenced in nvs_storage_iface struct but we override with ram_storage
bool nvs_has_key(const char *key) { (void)key; return false; }
esp_err_t nvs_write_str(const char *key, const char *value) { (void)key; (void)value; return ESP_FAIL; }
esp_err_t nvs_read_str(const char *key, char *value) { (void)key; (void)value; return ESP_ERR_NOT_FOUND; }
