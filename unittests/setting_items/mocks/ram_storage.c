#include "ram_storage.h"
#include "esp_err.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ITEM_NUM    100
#define MAX_KEY_LEN     32
#define MAX_STR_LEN     64

typedef struct {
    char key[MAX_KEY_LEN];
    char value[MAX_STR_LEN];
} storage_item_t;

bool mock_rams_write_str_called = false;

int mock_storage_read_error_code = ESP_OK;
int mock_storage_write_error_code = ESP_OK;

static storage_item_t storage[MAX_ITEM_NUM];

void rams_init()
{
    for (int i = 0; i < MAX_ITEM_NUM; i++) {
        storage[i].key[0] = '\0';
        storage[i].value[0] = '\0';
    }
    mock_rams_write_str_called = false;
}

bool rams_has_key(const char* key)
{
    if (!key) return false;

    for (int i = 0; i < MAX_ITEM_NUM; i++) {
        if (strncmp(storage[i].key, key, MAX_KEY_LEN) == 0) {
            return true;
        }
    }
    return false;
}

int rams_write_str(const char* key, const char* value)
{
    mock_rams_write_str_called = true;

    if (mock_storage_write_error_code != ESP_OK) {
        return mock_storage_write_error_code;
    }

    if (!key || !value) return ESP_ERR_INVALID_ARG;

    // Try to find existing key first
    for (int i = 0; i < MAX_ITEM_NUM; i++) {
        if (strncmp(storage[i].key, key, MAX_KEY_LEN) == 0) {
            strncpy(storage[i].value, value, MAX_STR_LEN - 1);
            storage[i].value[MAX_STR_LEN - 1] = '\0';
            return ESP_OK;
        }
    }

    // Find empty slot
    for (int i = 0; i < MAX_ITEM_NUM; i++) {
        if (storage[i].key[0] == '\0') {
            strncpy(storage[i].key, key, MAX_KEY_LEN - 1);
            storage[i].key[MAX_KEY_LEN - 1] = '\0';
            strncpy(storage[i].value, value, MAX_STR_LEN - 1);
            storage[i].value[MAX_STR_LEN - 1] = '\0';
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

int rams_read_str(const char* key, char* value)
{
    if (mock_storage_read_error_code != ESP_OK) {
        return mock_storage_read_error_code;
    }

    if (!key || !value) return ESP_ERR_INVALID_ARG;

    for (int i = 0; i < MAX_ITEM_NUM; i++) {
        if (strncmp(storage[i].key, key, MAX_KEY_LEN) == 0) {
            strncpy(value, storage[i].value, MAX_STR_LEN - 1);
            value[MAX_STR_LEN - 1] = '\0';
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}
