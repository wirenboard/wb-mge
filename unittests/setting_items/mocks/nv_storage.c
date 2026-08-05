#include "nv_storage.h"

esp_err_t nvs_begin_read_session(void) { return ESP_OK; }
void nvs_end_read_session(void) {}
bool nvs_has_key(const char *key) { (void)key; return false; }
esp_err_t nvs_write_str(const char *key, const char *value) { (void)key; (void)value; return ESP_FAIL; }
esp_err_t nvs_read_str(const char *key, char *value) { (void)key; (void)value; return ESP_ERR_NOT_FOUND; }
esp_err_t nvs_erase_str(const char *key) { (void)key; return ESP_OK; }
