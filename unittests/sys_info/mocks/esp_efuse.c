#include "esp_efuse.h"
#include "wb_app_desc.h"

#include <string.h>

esp_err_t mock_esp_efuse_read_block_return = ESP_OK;
static char mock_device_signature[DEVICE_SIGNATURE_LEN + 1] = TEST_DEVICE_SIGNATURE;

void mock_esp_efuse_set_signature(const char* signature)
{
    strncpy(mock_device_signature, signature, sizeof(mock_device_signature) - 1);
    mock_device_signature[sizeof(mock_device_signature) - 1] = '\0';
}

esp_err_t esp_efuse_read_block(esp_efuse_block_t blk, void* dst_key, size_t offset_in_bits, size_t size_bits)
{
    (void)blk;
    (void)offset_in_bits;

    if (mock_esp_efuse_read_block_return != ESP_OK) {
        return mock_esp_efuse_read_block_return;
    }

    size_t requested_bytes = size_bits / 8;
    size_t available_bytes = strlen(mock_device_signature);

    size_t bytes_to_copy = 0;
    if (requested_bytes < available_bytes) {
        bytes_to_copy = requested_bytes;
    } else {
        bytes_to_copy = available_bytes;
    }

    memcpy(dst_key, mock_device_signature, bytes_to_copy);

    if (bytes_to_copy < requested_bytes) {
        ((char*)dst_key)[bytes_to_copy] = '\0';
    }

    return ESP_OK;
}
