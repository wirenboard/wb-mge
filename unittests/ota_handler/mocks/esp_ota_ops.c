#include "esp_ota_ops.h"

/* The partition esp_ota_get_next_update_partition() hands out by default. On the device this is
 * always the slot the firmware is NOT running from. */
static const esp_partition_t default_partition = { .label = "ota_1" };

static const esp_partition_t *next_partition = &default_partition;

static esp_err_t begin_result = ESP_OK;
static esp_err_t end_result = ESP_OK;
static esp_err_t set_boot_partition_result = ESP_OK;

int mock_esp_ota_begin_call_count = 0;
int mock_esp_ota_write_call_count = 0;
int mock_esp_ota_end_call_count = 0;
int mock_esp_ota_abort_call_count = 0;
int mock_esp_ota_set_boot_partition_call_count = 0;
int mock_esp_ota_written_bytes = 0;

void mock_esp_ota_set_next_partition(const esp_partition_t *partition)
{
    next_partition = partition;
}

void mock_esp_ota_set_begin_result(esp_err_t result)
{
    begin_result = result;
}

void mock_esp_ota_set_end_result(esp_err_t result)
{
    end_result = result;
}

void mock_esp_ota_set_set_boot_partition_result(esp_err_t result)
{
    set_boot_partition_result = result;
}

void mock_esp_ota_reset(void)
{
    next_partition = &default_partition;
    begin_result = ESP_OK;
    end_result = ESP_OK;
    set_boot_partition_result = ESP_OK;
    mock_esp_ota_begin_call_count = 0;
    mock_esp_ota_write_call_count = 0;
    mock_esp_ota_end_call_count = 0;
    mock_esp_ota_abort_call_count = 0;
    mock_esp_ota_set_boot_partition_call_count = 0;
    mock_esp_ota_written_bytes = 0;
}

const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *start_from)
{
    (void)start_from;
    return next_partition;
}

esp_err_t esp_ota_begin(const esp_partition_t *partition, size_t image_size, esp_ota_handle_t *out_handle)
{
    (void)partition;
    (void)image_size;
    mock_esp_ota_begin_call_count++;
    if (out_handle != NULL) {
        *out_handle = 1;
    }
    return begin_result;
}

esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size)
{
    (void)handle;
    (void)data;
    mock_esp_ota_write_call_count++;
    mock_esp_ota_written_bytes += (int)size;
    return ESP_OK;
}

esp_err_t esp_ota_end(esp_ota_handle_t handle)
{
    (void)handle;
    mock_esp_ota_end_call_count++;
    return end_result;
}

esp_err_t esp_ota_abort(esp_ota_handle_t handle)
{
    (void)handle;
    mock_esp_ota_abort_call_count++;
    return ESP_OK;
}

esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition)
{
    (void)partition;
    mock_esp_ota_set_boot_partition_call_count++;
    return set_boot_partition_result;
}
