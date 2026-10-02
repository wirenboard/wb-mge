#include "nvs.h"

#include <stdio.h>
#include <string.h>

#define NVS_FAKE_KEYS_MAX           256
#define NVS_FAKE_KEY_LEN            16
#define NVS_FAKE_HANDLE             0x4E5653u

typedef struct {
    bool     used;
    bool     is_blob;
    char     key[NVS_FAKE_KEY_LEN];
    uint8_t  data[NVS_FAKE_BLOB_MAX];
    size_t   len;
    uint32_t u32;
} nvs_fake_entry_t;

static nvs_fake_entry_t nvs_fake_entries[NVS_FAKE_KEYS_MAX];

esp_err_t nvs_fake_open_result = ESP_OK;
esp_err_t nvs_fake_set_blob_result = ESP_OK;
esp_err_t nvs_fake_set_u32_result = ESP_OK;
esp_err_t nvs_fake_get_u32_result = ESP_OK;
esp_err_t nvs_fake_commit_result = ESP_OK;

int  nvs_fake_open_calls = 0;
int  nvs_fake_set_blob_calls = 0;
int  nvs_fake_commit_calls = 0;
char nvs_fake_opened_partition[32];
char nvs_fake_opened_namespace[32];
nvs_open_mode_t nvs_fake_opened_mode = NVS_READONLY;

static nvs_fake_entry_t *nvs_fake_find(const char *key)
{
    for (size_t i = 0; i < NVS_FAKE_KEYS_MAX; i++) {
        if (nvs_fake_entries[i].used && (strcmp(nvs_fake_entries[i].key, key) == 0)) {
            return &nvs_fake_entries[i];
        }
    }
    return NULL;
}

static nvs_fake_entry_t *nvs_fake_slot(const char *key)
{
    nvs_fake_entry_t *entry = nvs_fake_find(key);
    if (entry != NULL) {
        return entry;
    }
    for (size_t i = 0; i < NVS_FAKE_KEYS_MAX; i++) {
        if (!nvs_fake_entries[i].used) {
            entry = &nvs_fake_entries[i];
            memset(entry, 0, sizeof(*entry));
            entry->used = true;
            snprintf(entry->key, sizeof(entry->key), "%s", key);
            return entry;
        }
    }
    return NULL;
}

void nvs_fake_reset(void)
{
    memset(nvs_fake_entries, 0, sizeof(nvs_fake_entries));
    nvs_fake_open_result = ESP_OK;
    nvs_fake_set_blob_result = ESP_OK;
    nvs_fake_set_u32_result = ESP_OK;
    nvs_fake_get_u32_result = ESP_OK;
    nvs_fake_commit_result = ESP_OK;
    nvs_fake_open_calls = 0;
    nvs_fake_set_blob_calls = 0;
    nvs_fake_commit_calls = 0;
    nvs_fake_opened_partition[0] = '\0';
    nvs_fake_opened_namespace[0] = '\0';
    nvs_fake_opened_mode = NVS_READONLY;
}

void nvs_fake_put_blob(const char *key, const void *data, size_t len)
{
    nvs_fake_entry_t *entry = nvs_fake_slot(key);
    if ((entry == NULL) || (len > NVS_FAKE_BLOB_MAX)) {
        return;
    }
    entry->is_blob = true;
    memcpy(entry->data, data, len);
    entry->len = len;
}

void nvs_fake_put_u32(const char *key, uint32_t value)
{
    nvs_fake_entry_t *entry = nvs_fake_slot(key);
    if (entry == NULL) {
        return;
    }
    entry->is_blob = false;
    entry->u32 = value;
}

bool nvs_fake_peek_blob(const char *key, void *out, size_t *len)
{
    const nvs_fake_entry_t *entry = nvs_fake_find(key);
    if ((entry == NULL) || (!entry->is_blob) || (*len < entry->len)) {
        return false;
    }
    memcpy(out, entry->data, entry->len);
    *len = entry->len;
    return true;
}

bool nvs_fake_peek_u32(const char *key, uint32_t *value)
{
    const nvs_fake_entry_t *entry = nvs_fake_find(key);
    if ((entry == NULL) || entry->is_blob) {
        return false;
    }
    *value = entry->u32;
    return true;
}

esp_err_t nvs_open_from_partition(const char *part_name, const char *namespace_name, nvs_open_mode_t open_mode,
                                  nvs_handle_t *out_handle)
{
    nvs_fake_open_calls++;
    snprintf(nvs_fake_opened_partition, sizeof(nvs_fake_opened_partition), "%s", part_name);
    snprintf(nvs_fake_opened_namespace, sizeof(nvs_fake_opened_namespace), "%s", namespace_name);
    nvs_fake_opened_mode = open_mode;
    if (nvs_fake_open_result != ESP_OK) {
        return nvs_fake_open_result;
    }
    *out_handle = NVS_FAKE_HANDLE;
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value, size_t *length)
{
    if (handle != NVS_FAKE_HANDLE) {
        return ESP_ERR_INVALID_ARG;
    }
    const nvs_fake_entry_t *entry = nvs_fake_find(key);
    if (entry == NULL) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (!entry->is_blob) {
        return ESP_ERR_NVS_TYPE_MISMATCH;
    }
    if (out_value == NULL) {
        *length = entry->len;
        return ESP_OK;
    }
    if (*length < entry->len) {
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    memcpy(out_value, entry->data, entry->len);
    *length = entry->len;
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length)
{
    nvs_fake_set_blob_calls++;
    if (handle != NVS_FAKE_HANDLE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (nvs_fake_set_blob_result != ESP_OK) {
        return nvs_fake_set_blob_result;
    }
    if (length > NVS_FAKE_BLOB_MAX) {
        return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    }
    nvs_fake_put_blob(key, value, length);
    return ESP_OK;
}

esp_err_t nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value)
{
    if (handle != NVS_FAKE_HANDLE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (nvs_fake_get_u32_result != ESP_OK) {
        return nvs_fake_get_u32_result;
    }
    const nvs_fake_entry_t *entry = nvs_fake_find(key);
    if (entry == NULL) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (entry->is_blob) {
        return ESP_ERR_NVS_TYPE_MISMATCH;
    }
    *out_value = entry->u32;
    return ESP_OK;
}

esp_err_t nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value)
{
    if (handle != NVS_FAKE_HANDLE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (nvs_fake_set_u32_result != ESP_OK) {
        return nvs_fake_set_u32_result;
    }
    nvs_fake_put_u32(key, value);
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    nvs_fake_commit_calls++;
    if (handle != NVS_FAKE_HANDLE) {
        return ESP_ERR_INVALID_ARG;
    }
    return nvs_fake_commit_result;
}
