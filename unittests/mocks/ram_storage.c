#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ram_storage.h"

#define SETTING_ITEM_NUM_MAX 100
#define MAX_KEY_LEN 32
#define MAX_STR_LEN 128

typedef enum {
    TYPE_STR,
    TYPE_U8,
    TYPE_U16,
    TYPE_U32
} value_type_t;

typedef struct {
    char key[MAX_KEY_LEN];
    value_type_t type;
    union {
        char str_val[MAX_STR_LEN];
        uint8_t u8_val;
        uint16_t u16_val;
        uint32_t u32_val;
    } value;
} storage_item_t;

storage_item_t storage[SETTING_ITEM_NUM_MAX];

void init_storage() {
    for (int i = 0; i < SETTING_ITEM_NUM_MAX; i++) {
        storage[i].key[0] = '\0';
    }
}

esp_err_t rams_write_str(const char* key, const char* value) {
    for (int i = 0; i < SETTING_ITEM_NUM_MAX; i++) {
        if (storage[i].key[0] == '\0' || strcmp(storage[i].key, key) == 0) {
            strncpy(storage[i].key, key, MAX_KEY_LEN);
            storage[i].type = TYPE_STR;
            strncpy(storage[i].value.str_val, value, MAX_STR_LEN);
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t rams_read_str(const char* key, char* value) {
    for (int i = 0; i < SETTING_ITEM_NUM_MAX; i++) {
        if (strcmp(storage[i].key, key) == 0 && storage[i].type == TYPE_STR) {
            strncpy(value, storage[i].value.str_val, MAX_STR_LEN);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t rams_write_u8(const char* key, uint8_t value) {
    for (int i = 0; i < SETTING_ITEM_NUM_MAX; i++) {
        if (storage[i].key[0] == '\0' || strcmp(storage[i].key, key) == 0) {
            strncpy(storage[i].key, key, MAX_KEY_LEN);
            storage[i].type = TYPE_U8;
            storage[i].value.u8_val = value;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t rams_read_u8(const char* key, uint8_t* value) {
    for (int i = 0; i < SETTING_ITEM_NUM_MAX; i++) {
        if (strcmp(storage[i].key, key) == 0 && storage[i].type == TYPE_U8) {
            *value = storage[i].value.u8_val;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t rams_write_u16(const char* key, uint16_t value) {
    for (int i = 0; i < SETTING_ITEM_NUM_MAX; i++) {
        if (storage[i].key[0] == '\0' || strcmp(storage[i].key, key) == 0) {
            strncpy(storage[i].key, key, MAX_KEY_LEN);
            storage[i].type = TYPE_U16;
            storage[i].value.u16_val = value;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t rams_read_u16(const char* key, uint16_t* value) {
    for (int i = 0; i < SETTING_ITEM_NUM_MAX; i++) {
        if (strcmp(storage[i].key, key) == 0 && storage[i].type == TYPE_U16) {
            *value = storage[i].value.u16_val;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t rams_write_u32(const char* key, uint32_t value) {
    for (int i = 0; i < SETTING_ITEM_NUM_MAX; i++) {
        if (storage[i].key[0] == '\0' || strcmp(storage[i].key, key) == 0) {
            strncpy(storage[i].key, key, MAX_KEY_LEN);
            storage[i].type = TYPE_U32;
            storage[i].value.u32_val = value;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t rams_read_u32(const char* key, uint32_t* value) {
    for (int i = 0; i < SETTING_ITEM_NUM_MAX; i++) {
        if (strcmp(storage[i].key, key) == 0 && storage[i].type == TYPE_U32) {
            *value = storage[i].value.u32_val;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}
