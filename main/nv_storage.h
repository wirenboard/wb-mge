#pragma once

#include <esp_err.h>
#include <stdbool.h>

esp_err_t nvs_init(void);

esp_err_t nvs_write_str(const char* key, const char* value);
esp_err_t nvs_read_str(const char* key, char* value);

esp_err_t nvs_write_blob(const char* key, const void* buf, size_t buf_size);
esp_err_t nvs_read_blob(const char* key, void* buf, size_t* buf_size);

bool nvs_has_key(const char* key);
