#pragma once

typedef enum {
    ESP_OK = 0,
    ESP_ERR_NOT_FOUND,
    ESP_ERR_NO_MEM,
    ESP_ERR_INVALID_ARG
} esp_err_t;

esp_err_t rams_init(void);
esp_err_t rams_write_str(const char* key, const char* value);
esp_err_t rams_read_str(const char* key, char* value);
esp_err_t rams_write_u8(const char* key, uint8_t value);
esp_err_t rams_read_u8(const char* key, uint8_t* value);
esp_err_t rams_write_u16(const char* key, uint16_t value);
esp_err_t rams_read_u16(const char* key, uint16_t* value);
esp_err_t rams_write_u32(const char* key, uint32_t value);
esp_err_t rams_read_u32(const char* key, uint32_t* value);
