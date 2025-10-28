#include "unity.h"

#include "esp_efuse.h"
#include "wb_app_desc.h"

#include <string.h>
#include <stdint.h>

#define BLOCK_SIZE                      96

static uint8_t efuse_blocks[EFUSE_BLK_MAX][BLOCK_SIZE];
esp_efuse_block_t mock_read_block = EFUSE_BLK0;
size_t mock_read_offset = 0;
esp_err_t mock_esp_efuse_read_block_return = ESP_OK;

static void mock_esp_efuse_write_signature(
    esp_efuse_block_t blk, const void* src_key, size_t offset_in_bits, size_t size_bits
)
{
    size_t offset_bytes = offset_in_bits / 8;
    size_t size_bytes = size_bits / 8;

    memcpy(&efuse_blocks[blk][offset_bytes], src_key, size_bytes);
}

void mock_esp_efuse_set_signature(const char* signature)
{
    uint8_t sig_buffer[DEVICE_SIGNATURE_LEN];
    memset(sig_buffer, 0, DEVICE_SIGNATURE_LEN);

    size_t sig_len = strlen(signature);
    if (sig_len > DEVICE_SIGNATURE_LEN) {
        sig_len = DEVICE_SIGNATURE_LEN;
    }

    memcpy(sig_buffer, signature, sig_len);
    mock_esp_efuse_write_signature(SIGNATURE_BLOCK, sig_buffer, SIGNATURE_OFFSET_BITS, DEVICE_SIGNATURE_LEN * 8);
}

esp_err_t esp_efuse_read_block(esp_efuse_block_t blk, void* dst_key, size_t offset_in_bits, size_t size_bits)
{
    mock_read_block = blk;
    mock_read_offset = offset_in_bits;

    if (mock_esp_efuse_read_block_return != ESP_OK)
        return mock_esp_efuse_read_block_return;

    size_t offset_bytes = offset_in_bits / 8;
    size_t size_bytes = size_bits / 8;

    TEST_ASSERT_LESS_THAN_INT_MESSAGE(BLOCK_SIZE, offset_bytes + size_bytes, "Read exceeds block size");

    memcpy(dst_key, &efuse_blocks[blk][offset_bytes], size_bytes);

    return ESP_OK;
}

void mock_esp_efuse_reset(void)
{
    mock_esp_read_mac_should_fail = false;
    mock_read_block = EFUSE_BLK0;
    mock_read_offset = 0;
    mock_esp_efuse_read_block_return = ESP_OK;
    mock_esp_efuse_set_signature(TEST_DEVICE_SIGNATURE);
}
