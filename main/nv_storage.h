#pragma once

#include <esp_err.h>
#include <stdbool.h>

esp_err_t nvs_init(void);

esp_err_t nvs_write_str(const char* key, const char* value);
esp_err_t nvs_read_str(const char* key, char* value);

bool nvs_has_key(const char* key);
