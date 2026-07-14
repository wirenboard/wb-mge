#pragma once

#include <esp_err.h>
#include <stdbool.h>

esp_err_t nvs_init(void);

esp_err_t nvs_write_str(const char* key, const char* value);
esp_err_t nvs_read_str(const char* key, char* value);

// Remove a key from NVS. An absent key is reported as success.
esp_err_t nvs_erase_str(const char* key);

bool nvs_has_key(const char* key);
