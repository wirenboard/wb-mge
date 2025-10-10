#pragma once

#include "esp_err.h"
#include <stdbool.h>

bool nvs_has_key(const char *key);
esp_err_t nvs_write_str(const char *key, const char *value);
esp_err_t nvs_read_str(const char *key, char *value);
