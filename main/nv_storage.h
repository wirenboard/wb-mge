#pragma once

#include "esp_err.h"

esp_err_t nvs_init(void);
esp_err_t nvs_write_str(const char* key, const char* value);
esp_err_t nvs_read_str(const char* key, char* value);
esp_err_t nvs_write_u8(const char* key, uint8_t value);
esp_err_t nvs_read_u8(const char* key, uint8_t* value);
esp_err_t nvs_write_u16(const char* key, uint16_t value);
esp_err_t nvs_read_u16(const char* key, uint16_t* value);
esp_err_t nvs_write_u32(const char* key, uint32_t value);
esp_err_t nvs_read_u32(const char* key, uint32_t* value);
esp_err_t nvs_write_blob(const char* key, uint8_t* value, size_t size);
esp_err_t nvs_read_blob(const char* key, uint8_t* value, size_t* required_size);
