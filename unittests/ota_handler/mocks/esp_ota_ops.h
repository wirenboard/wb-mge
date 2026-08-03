#pragma once

/* Minimal stub of esp_ota_ops.h (and the piece of esp_partition.h it exposes) for the ota_handler
 * unit tests. Only the calls ota_handler.c makes are here; nothing writes to flash. */

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define OTA_SIZE_UNKNOWN    0xffffffff

typedef uint32_t esp_ota_handle_t;

typedef struct {
    char label[17];
} esp_partition_t;

const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *start_from);
esp_err_t esp_ota_begin(const esp_partition_t *partition, size_t image_size, esp_ota_handle_t *out_handle);
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size);
esp_err_t esp_ota_end(esp_ota_handle_t handle);
esp_err_t esp_ota_abort(esp_ota_handle_t handle);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition);

/* --- mock control and observation ---------------------------------------- */

extern int mock_esp_ota_begin_call_count;
extern int mock_esp_ota_write_call_count;
extern int mock_esp_ota_end_call_count;
extern int mock_esp_ota_abort_call_count;
extern int mock_esp_ota_set_boot_partition_call_count;
extern int mock_esp_ota_written_bytes;

/* NULL makes esp_ota_get_next_update_partition() report "no OTA partition". */
void mock_esp_ota_set_next_partition(const esp_partition_t *partition);

/* Result the corresponding call returns; ESP_OK by default. */
void mock_esp_ota_set_begin_result(esp_err_t result);
void mock_esp_ota_set_end_result(esp_err_t result);
void mock_esp_ota_set_set_boot_partition_result(esp_err_t result);

void mock_esp_ota_reset(void);
