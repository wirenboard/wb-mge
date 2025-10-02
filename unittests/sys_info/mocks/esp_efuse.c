#include "esp_efuse.h"
#include <string.h>

// Mock control variables
static esp_err_t mock_esp_efuse_read_block_return = ESP_OK;
static char mock_device_signature[16] = "TEST_SIG";

void mock_esp_efuse_set_signature(const char* signature)
{
    strncpy(mock_device_signature, signature, sizeof(mock_device_signature) - 1);
    mock_device_signature[sizeof(mock_device_signature) - 1] = '\0';
}

void mock_esp_efuse_set_read_return(esp_err_t ret)
{
    mock_esp_efuse_read_block_return = ret;
}

esp_err_t esp_efuse_read_block(esp_efuse_block_t blk, void* dst_key, size_t offset_in_bits, size_t size_bits)
{
    (void)blk;
    (void)offset_in_bits;
    
    if (mock_esp_efuse_read_block_return == ESP_OK && dst_key) {
        size_t bytes_to_copy = size_bits / 8;
        if (bytes_to_copy > strlen(mock_device_signature)) {
            bytes_to_copy = strlen(mock_device_signature);
        }
        memcpy(dst_key, mock_device_signature, bytes_to_copy);
        // Null-terminate if there's space
        if (size_bits / 8 > bytes_to_copy) {
            ((char*)dst_key)[bytes_to_copy] = '\0';
        }
    }
    return mock_esp_efuse_read_block_return;
}
