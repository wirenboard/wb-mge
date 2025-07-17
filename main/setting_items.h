#pragma once

#include <esp_err.h>
#include <stdbool.h>

#define SETTING_ITEM_MAX_STR_LEN 32

// Storage interface for dependency injection (mainly for testing)
typedef struct {
    bool (*has_key)(const char* key);
    esp_err_t (*write_str)(const char* key, const char* value);
    esp_err_t (*read_str)(const char* key, char* value);
} setting_storage_iface_t;

// Core functions
esp_err_t setting_items_init(void);
esp_err_t setting_items_init_with_storage(const setting_storage_iface_t *storage_iface);
esp_err_t setting_items_save(const char *key, const char *value);
esp_err_t setting_items_read(const char *key, char *value);

// Convenience wrapper functions for common types
uint32_t setting_items_read_u32(const char *key);
bool setting_items_read_bool(const char *key);
int setting_items_read_int(const char *key);

esp_err_t setting_items_save_u32(const char *key, uint32_t value);
esp_err_t setting_items_save_bool(const char *key, bool value);
esp_err_t setting_items_save_int(const char *key, int value);
bool setting_items_has_key(const char *key);
esp_err_t setting_items_set_default(const char *key);
const char *setting_items_get_default(const char *key);

// Iterator functions for all settings
size_t setting_items_get_count(void);
const char *setting_items_get_key_at(size_t index);
bool setting_items_is_private(const char *key);
