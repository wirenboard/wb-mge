#ifndef NV_STORAGE_H_
#define NV_STORAGE_H_

// Empty stub - setting_items.c includes this but we use setting_items_init_with_storage()
// so these functions are never called in unit tests

#include "esp_err.h"
#include <stdbool.h>

// Stub declarations (never called)
bool nvs_has_key(const char *key);
esp_err_t nvs_write_str(const char *key, const char *value);
esp_err_t nvs_read_str(const char *key, char *value);

#endif /* NV_STORAGE_H_ */