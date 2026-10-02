#pragma once

/*
 * An in-memory NVS namespace for fp_log: blobs and u32 values by key, with the semantics fp_log
 * relies on (a missing key is ESP_ERR_NVS_NOT_FOUND, a blob larger than the buffer is
 * ESP_ERR_NVS_INVALID_LENGTH, a smaller one is read with its own length), and a way to make each
 * call fail. The error codes are ESP-IDF's own (components/nvs_flash/include/nvs.h).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define ESP_ERR_NVS_BASE                0x1100
#define ESP_ERR_NVS_NOT_INITIALIZED     (ESP_ERR_NVS_BASE + 0x01)
#define ESP_ERR_NVS_NOT_FOUND           (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_TYPE_MISMATCH       (ESP_ERR_NVS_BASE + 0x03)
#define ESP_ERR_NVS_NOT_ENOUGH_SPACE    (ESP_ERR_NVS_BASE + 0x05)
#define ESP_ERR_NVS_INVALID_LENGTH      (ESP_ERR_NVS_BASE + 0x0c)

typedef uint32_t nvs_handle_t;

typedef enum {
    NVS_READONLY,
    NVS_READWRITE,
} nvs_open_mode_t;

esp_err_t nvs_open_from_partition(const char *part_name, const char *namespace_name, nvs_open_mode_t open_mode,
                                  nvs_handle_t *out_handle);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value, size_t *length);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length);
esp_err_t nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value);
esp_err_t nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value);
esp_err_t nvs_commit(nvs_handle_t handle);

// ── Test side ────────────────────────────────────────────────────────────────────────────

#define NVS_FAKE_BLOB_MAX               128

// What each call returns instead of doing its work; ESP_OK lets it work.
extern esp_err_t nvs_fake_open_result;
extern esp_err_t nvs_fake_set_blob_result;
extern esp_err_t nvs_fake_set_u32_result;
extern esp_err_t nvs_fake_get_u32_result;
extern esp_err_t nvs_fake_commit_result;

extern int  nvs_fake_open_calls;
extern int  nvs_fake_set_blob_calls;
extern int  nvs_fake_commit_calls;
extern char nvs_fake_opened_partition[32];
extern char nvs_fake_opened_namespace[32];
extern nvs_open_mode_t nvs_fake_opened_mode;

void nvs_fake_reset(void);
// Seeds a key as an earlier boot (or firmware) left it.
void nvs_fake_put_blob(const char *key, const void *data, size_t len);
void nvs_fake_put_u32(const char *key, uint32_t value);
// Reads a key back as stored; false when it is not there (or holds the other type).
bool nvs_fake_peek_blob(const char *key, void *out, size_t *len);
bool nvs_fake_peek_u32(const char *key, uint32_t *value);
